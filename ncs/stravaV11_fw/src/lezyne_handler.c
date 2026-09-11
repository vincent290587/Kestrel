#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <zephyr/logging/log.h>

#include "l_protocol.h"
#include "lezyne_ble.h"
#include "lezyne_handler.h"

/* WRN, not the usual per-module INF this port defaults to elsewhere: a
 * real-hardware flash-overflow test showed this file's own LOG_INF()
 * narration (each one's format string + call machinery is compiled in
 * regardless of runtime log level, unless filtered out at this
 * compile-time module level) cost real, needed bytes against this
 * board's thin partition margin -- see lezyne_ble.c's own history in git
 * log for the larger story. "LEZ STATUS" (cmd_console.c) still gives an
 * on-demand snapshot without needing INF-level boot narration. */
LOG_MODULE_REGISTER(lezyne_handler, LOG_LEVEL_WRN);

/* stravaV10's BLE_NUS_STD_DATA_LEN default (ATT MTU 23 - 3 bytes header,
 * never renegotiated up in the original SDK16 code) -- used for every
 * fixed-layout control packet (status/list/delete/...). FIT data chunks
 * use the real negotiated MTU instead (lezyne_ble_get_mtu()), same as
 * stravaV10's own NUS_LONG_PACKETS_SIZE/mtu_length distinction in
 * app_packets_handler.c's _handle_file_upload(). */
#define LEZ_STD_LEN 20
#define LEZ_QUEUE_ITEM_MAX 240
#define LEZ_QUEUE_DEPTH 32

struct lez_queue_item {
	uint8_t data[LEZ_QUEUE_ITEM_MAX];
	uint16_t len;
};

static struct lez_queue_item m_queue[LEZ_QUEUE_DEPTH];
static uint8_t m_queue_head; /* next slot to pop */
static uint8_t m_queue_tail; /* next slot to push */
static uint8_t m_queue_count;

static bool m_connected;

/* FIT download state -- the one genuinely long-running, resumable piece
 * (a ride file can be many MB, see ride_recorder.h's 2MB/slot budget), so
 * unlike file listing (done synchronously below) this is driven from the
 * periodic tick. */
static bool m_fit_download_active;
static struct fs_file_t m_fit_file;
static uint32_t m_fit_file_id;
static uint32_t m_fit_remaining;
static bool m_fit_final_chunk_sent; /* true once the last data chunk (possibly
				      * partial) is queued -- one more tick then
				      * sends the empty FitFileTransferEnd
				      * terminator, matching stravaV10's own
				      * two-step tail in _handle_file_upload(). */

/* One shared mount instance for the whole download session (start ->
 * finish/abort) -- Zephyr's fs_unmount() matches by the exact struct
 * fs_mount_t pointer used at fs_mount() time (an intrusive dnode embedded
 * in it, not a mnt_point string lookup), so start/finish/abort MUST all
 * use the same instance or fs_unmount() silently no-ops ("fs not mounted")
 * and the mount point stays held forever -- exactly the "filesystem mount
 * conflict" bug class CLAUDE.md already documents once for
 * map_screen_demo.c/sd_stress_demo.c, avoided here by construction. */
static FATFS m_fit_fat_fs;
static struct fs_mount_t m_fit_mp = {
	.type = FS_FATFS,
	.fs_data = &m_fit_fat_fs,
	.mnt_point = "/SD:",
};

static void lez_tick_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(lez_tick_work, lez_tick_handler);

#define LEZ_TICK_PERIOD_MS 50

static bool queue_push(const uint8_t *data, uint16_t len)
{
	if (m_queue_count >= LEZ_QUEUE_DEPTH || len > LEZ_QUEUE_ITEM_MAX) {
		LOG_WRN("lezyne_handler: TX queue full or packet too large (len=%u), dropping",
			len);
		return false;
	}

	struct lez_queue_item *item = &m_queue[m_queue_tail];

	memcpy(item->data, data, len);
	item->len = len;
	m_queue_tail = (m_queue_tail + 1) % LEZ_QUEUE_DEPTH;
	m_queue_count++;
	return true;
}

static void queue_reset(void)
{
	m_queue_head = 0;
	m_queue_tail = 0;
	m_queue_count = 0;
}

static void send_cmd_only(uint8_t cmd)
{
	uint8_t pkt[LEZ_STD_LEN] = { 0 };

	pkt[0] = cmd;
	queue_push(pkt, sizeof(pkt));
}

static void send_status_packet(void)
{
	uint8_t pkt[LEZ_STD_LEN] = { 0 };
	uint8_t idx = 0;

	/* 2026-09-11: reverted back to this 6-byte layout at the user's own
	 * request (their original scheme, matching stravaV10's own
	 * app_packets_handler.c _send_status_packet() byte-for-byte -- that
	 * legacy source itself has "valueOf8"/"model32"/"ble_speed8" fields
	 * commented out, i.e. deliberately not sent, not merely an oversight).
	 * A same-day, now-reverted version of this function instead matched
	 * the real "GPS Ally" app's own handleStatusPacket() parser exactly
	 * (decompiled from the real app -- see Lezyne_app.md/
	 * LezyneCycleComputerDevice.txt at the repo root), which reads this
	 * payload, after the opcode byte, as: [0]=state/recording code,
	 * [1]=GPS device model, [2]=unused/skipped, [3]=major_ver,
	 * [4]=minor_ver, [5]=isNavigating raw, [6]=gps_mode, [7]=ble_speed --
	 * worth returning to if the phone shows a garbled firmware version/
	 * model once connected, since this packet is sent from
	 * lezyne_handler_on_connected() and so can't be what's blocking GPS
	 * Ally from listing the device during its pre-connection BLE scan
	 * (that turned out to be isLezyneDevice()'s address-suffix check,
	 * see ble_demo.c's own top-of-function comment). */
	pkt[idx++] = RequestPhoneStatus;
	pkt[idx++] = 0;    /* padding8 */
	pkt[idx++] = 0x6;  /* major_ver8 */
	pkt[idx++] = 0xA;  /* minor_ver8 */
	pkt[idx++] = 0xB;  /* navigating8 */
	pkt[idx++] = 0x1E; /* gps_mode8 */

	queue_push(pkt, sizeof(pkt));
}

static void encode_uint32_le(uint32_t value, uint8_t *dst)
{
	dst[0] = (uint8_t)(value & 0xFF);
	dst[1] = (uint8_t)((value >> 8) & 0xFF);
	dst[2] = (uint8_t)((value >> 16) & 0xFF);
	dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t decode_uint32_le(const uint8_t *src)
{
	return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
	       ((uint32_t)src[3] << 24);
}

/* True for an 8-char hex name followed by ".FIT"/".fit" -- ride_recorder.c's
 * own export naming (fname = "/SD:/%08lX.FIT", unix timestamp in hex). */
static bool is_fit_filename(const char *name, uint32_t *out_id)
{
	size_t len = strlen(name);

	if (len != 12 || strcasecmp(name + 8, ".FIT") != 0) {
		return false;
	}
	for (int i = 0; i < 8; i++) {
		if (!isxdigit((unsigned char)name[i])) {
			return false;
		}
	}
	*out_id = (uint32_t)strtoul(name, NULL, 16);
	return true;
}

static void fit_path_for_id(uint32_t file_id, char *buf, size_t buf_len)
{
	snprintf(buf, buf_len, "/SD:/%08lX.FIT", (unsigned long)file_id);
}

/* Real round trip against ride_recorder.c's own export directory, same
 * transient-mount discipline every other SD-card user in this port follows
 * (sd_fat_demo/map_screen_demo/ride_recorder.c itself all mount, do one
 * operation, unmount -- see CLAUDE.md's "filesystem mount conflict" note
 * for why holding a persistent mount here would be a real bug). */
static int with_sd_mounted(int (*fn)(void *ctx), void *ctx)
{
	static FATFS fat_fs;
	static struct fs_mount_t mp = {
		.type = FS_FATFS,
		.fs_data = &fat_fs,
		.mnt_point = "/SD:",
	};

	int err = fs_mount(&mp);

	if (err) {
		LOG_ERR("lezyne_handler: fs_mount(/SD:) -> %d", err);
		return err;
	}

	int ret = fn(ctx);

	fs_unmount(&mp);
	return ret;
}

struct list_ctx {
	uint32_t ids[LEZ_QUEUE_DEPTH * 4]; /* generous upper bound, see handle_file_list() */
	size_t count;
};

static int list_fit_files(void *ctx_)
{
	struct list_ctx *ctx = ctx_;
	struct fs_dir_t dir;
	struct fs_dirent entry;

	fs_dir_t_init(&dir);
	int err = fs_opendir(&dir, "/SD:");

	if (err) {
		return err;
	}

	while (ctx->count < ARRAY_SIZE(ctx->ids) && fs_readdir(&dir, &entry) == 0 &&
	       entry.name[0] != '\0') {
		uint32_t id;

		if (entry.type == FS_DIR_ENTRY_FILE && is_fit_filename(entry.name, &id)) {
			ctx->ids[ctx->count++] = id;
		}
	}

	fs_closedir(&dir);
	return 0;
}

/* Matches stravaV10's sd_functions__query_fit_list() wire format exactly
 * (see source/sd/sd_functions.cpp): each FileListSending packet's payload
 * is [count_in_this_packet(u8)][file_id(u32 LE)]*count, as many files per
 * packet as fit in the STD packet length. */
static void handle_file_list(void)
{
	struct list_ctx ctx = { .count = 0 };

	int err = with_sd_mounted(list_fit_files, &ctx);

	if (err) {
		LOG_ERR("lezyne_handler: FIT file listing failed (%d)", err);
		return;
	}

	LOG_INF("lezyne_handler: listing %u FIT file(s)", (unsigned)ctx.count);

	/* pkt[0]=cmd, pkt[1]=count-in-this-packet(u8), pkt[2..]=file_id(u32 LE)*count
	 * -- matches sd_functions__query_fit_list()'s own layout exactly:
	 * c_array.str = &data_array[1], str[0] = nb_files, encode_uint32() at
	 * str[cur_size] starting cur_size=1 (i.e. str[1] = data_array[2]). */
	const size_t files_per_packet = (LEZ_STD_LEN - 2) / 4;
	size_t sent = 0;

	do {
		uint8_t pkt[LEZ_STD_LEN] = { 0 };
		size_t this_batch = ctx.count - sent;

		if (this_batch > files_per_packet) {
			this_batch = files_per_packet;
		}

		pkt[0] = FileListSending;
		pkt[1] = (uint8_t)this_batch;
		for (size_t i = 0; i < this_batch; i++) {
			encode_uint32_le(ctx.ids[sent + i], &pkt[2 + i * 4]);
		}
		queue_push(pkt, sizeof(pkt));
		sent += this_batch;
	} while (sent < ctx.count);

	if (ctx.count == 0) {
		/* Still send one (empty) FileListSending packet so the phone
		 * app's own state machine sees a response, matching the
		 * original's do/while(rem_bytes>0) always running at least once. */
		uint8_t pkt[LEZ_STD_LEN] = { 0 };

		pkt[0] = FileListSending;
		queue_push(pkt, sizeof(pkt));
	}
}

static int unlink_fn(void *ctx)
{
	return fs_unlink((const char *)ctx);
}

static void handle_file_delete(const uint8_t *data)
{
	uint32_t file_id = decode_uint32_le(data + 1);
	char path[24];

	fit_path_for_id(file_id, path, sizeof(path));

	int err = with_sd_mounted(unlink_fn, path);

	if (err) {
		LOG_ERR("lezyne_handler: delete %s failed (%d)", path, err);
		return;
	}

	uint8_t pkt[LEZ_STD_LEN] = { 0 };

	pkt[0] = FileDeleteConfirmation;
	encode_uint32_le(file_id, &pkt[1]);
	queue_push(pkt, sizeof(pkt));

	LOG_INF("lezyne_handler: deleted %s", path);
}

static void fit_download_abort(void)
{
	if (m_fit_download_active) {
		fs_close(&m_fit_file);
		fs_unmount(&m_fit_mp);
	}
	m_fit_download_active = false;
	m_fit_final_chunk_sent = false;
}

static void handle_file_download_start(const uint8_t *data)
{
	if (m_fit_download_active) {
		LOG_WRN("lezyne_handler: FIT download already in progress, aborting old one");
		fit_download_abort();
	}

	uint32_t file_id = decode_uint32_le(data + 1);
	char path[24];

	fit_path_for_id(file_id, path, sizeof(path));

	int err = fs_mount(&m_fit_mp);

	if (err) {
		LOG_ERR("lezyne_handler: fs_mount(/SD:) -> %d", err);
		return;
	}

	struct fs_dirent stat;

	err = fs_stat(path, &stat);
	if (err) {
		LOG_ERR("lezyne_handler: FIT file %s not found (%d)", path, err);
		fs_unmount(&m_fit_mp);
		return;
	}

	fs_file_t_init(&m_fit_file);
	err = fs_open(&m_fit_file, path, FS_O_READ);
	/* Deliberately leaves /SD: mounted for the duration of the transfer
	 * (unlike every other SD user in this file) -- a multi-MB ride file
	 * streamed a chunk at a time over many seconds/minutes needs the
	 * mount held, not re-acquired every tick; unmounted again once the
	 * transfer finishes or aborts (fit_download_abort()/fit_download_finish()
	 * below, both via the same m_fit_mp instance). No other module in this
	 * port holds /SD: open this long, so this is a deliberate, bounded
	 * exception to the transient-mount convention, not a reversion to the
	 * earlier persistent-mount bug. */
	if (err) {
		LOG_ERR("lezyne_handler: fs_open(%s) failed (%d)", path, err);
		fs_unmount(&m_fit_mp);
		return;
	}

	m_fit_file_id = file_id;
	m_fit_remaining = stat.size;
	m_fit_download_active = true;
	m_fit_final_chunk_sent = false;

	send_cmd_only(SwitchingToHighSpeed);
	send_cmd_only(ConnectedInHighSpeed);

	uint8_t pkt[LEZ_STD_LEN] = { 0 };

	pkt[0] = FitFileTransferStart;
	encode_uint32_le(file_id, &pkt[1]);
	encode_uint32_le(stat.size, &pkt[5]);
	queue_push(pkt, sizeof(pkt));

	LOG_INF("lezyne_handler: FIT download start %s (%u bytes)", path, (unsigned)stat.size);
}

static void fit_download_finish(void)
{
	fs_close(&m_fit_file);
	fs_unmount(&m_fit_mp);
	m_fit_download_active = false;
	m_fit_final_chunk_sent = false;

	send_cmd_only(SwitchingToLowSpeed);
	send_cmd_only(ConnectedInLowSpeed);
	send_status_packet();

	LOG_INF("lezyne_handler: FIT download %08lX complete", (unsigned long)m_fit_file_id);
}

/* Advances an in-progress FIT download by at most one chunk per tick, only
 * when there's queue room -- called from lez_tick_handler(). */
static void fit_download_tick(void)
{
	if (!m_fit_download_active || m_queue_count >= LEZ_QUEUE_DEPTH) {
		return;
	}

	if (m_fit_final_chunk_sent) {
		uint8_t pkt[LEZ_STD_LEN] = { 0 };

		pkt[0] = FitFileTransferEnd;
		if (queue_push(pkt, sizeof(pkt))) {
			fit_download_finish();
		}
		return;
	}

	uint16_t mtu_len = lezyne_ble_get_mtu();
	uint16_t chunk_len = mtu_len < LEZ_STD_LEN ? mtu_len : LEZ_STD_LEN;

	if (chunk_len < 2) {
		chunk_len = LEZ_STD_LEN;
	}

	uint8_t pkt[LEZ_QUEUE_ITEM_MAX] = { 0 };
	uint16_t data_cap = chunk_len - 1;

	if (data_cap > m_fit_remaining) {
		data_cap = (uint16_t)m_fit_remaining;
	}

	ssize_t got = fs_read(&m_fit_file, &pkt[1], data_cap);

	if (got < 0) {
		LOG_ERR("lezyne_handler: fs_read() failed (%d), aborting FIT download", (int)got);
		fit_download_abort();
		return;
	}

	m_fit_remaining -= (uint32_t)got;
	pkt[0] = FitFileTransferData;

	if (queue_push(pkt, (uint16_t)(1 + got))) {
		if (m_fit_remaining == 0 || (size_t)got < data_cap) {
			m_fit_final_chunk_sent = true;
		}
	}
}

static void lez_tick_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (m_queue_count > 0) {
		struct lez_queue_item *item = &m_queue[m_queue_head];
		int err = lezyne_ble_send(item->data, item->len);

		if (err == 0) {
			m_queue_head = (m_queue_head + 1) % LEZ_QUEUE_DEPTH;
			m_queue_count--;
		} else if (err != -ENOMEM && err != -EAGAIN && err != -ENOTCONN &&
			   err != -EINVAL) {
			/* A real, non-transient send error -- drop the packet
			 * rather than retry forever. -EINVAL is deliberately
			 * treated as transient, not fatal: confirmed on real
			 * hardware (a real BLE central connected and this fired
			 * immediately) that bt_nus_send()/nrf/subsys/bluetooth/
			 * services/nus.c returns exactly -EINVAL whenever the
			 * peer hasn't subscribed to the NUS TX characteristic's
			 * notifications yet (bt_gatt_is_subscribed() check) --
			 * completely normal for the first tick or two right
			 * after connect, since a central's own CCCD write
			 * generally lands shortly after its GATT discovery
			 * completes, not before. Retrying the same head-of-queue
			 * packet costs nothing (the queue doesn't grow unbounded
			 * from this -- a real disconnect resets it via
			 * lezyne_handler_on_disconnected()) and lets it send
			 * cleanly the moment the peer actually subscribes. */
			LOG_WRN("lezyne_handler: send failed (%d), dropping packet", err);
			m_queue_head = (m_queue_head + 1) % LEZ_QUEUE_DEPTH;
			m_queue_count--;
		}
		/* else: transient backpressure, retry same packet next tick. */
	}

	fit_download_tick();

	k_work_schedule(&lez_tick_work, K_MSEC(LEZ_TICK_PERIOD_MS));
}

void lezyne_handler_init(void)
{
	k_work_schedule(&lez_tick_work, K_MSEC(LEZ_TICK_PERIOD_MS));
}

void lezyne_handler_on_connected(void)
{
	m_connected = true;
	queue_reset();
	fit_download_abort();

	send_cmd_only(ConnectedInLowSpeed);
	send_status_packet();

	LOG_INF("lezyne_handler: connected, status queued");
}

void lezyne_handler_on_disconnected(void)
{
	m_connected = false;
	fit_download_abort();
	queue_reset();
}

void lezyne_handler_on_rx(const uint8_t *data, uint16_t length)
{
	if (length == 0) {
		return;
	}

	uint8_t cmd = data[0];

	switch (cmd) {
	case RequestFitFileList:
		handle_file_list();
		break;

	case RequestFitFileDownload:
		handle_file_download_start(data);
		break;

	case RequestFitFileDelete:
		handle_file_delete(data);
		break;

	case SettingsRequestV1:
	case SettingsRequest:
		LOG_INF("lezyne_handler: settings sync requested (not implemented yet)");
		break;

	case PhoneStatus:
		if (length >= 4) {
			LOG_INF("lezyne_handler: phone status GMT+%d live_tracking=%u",
				(int8_t)data[2] / 4, data[3]);
		}
		break;

	case NewSegmentListReady:
	case SegmentUpdateCancel:
	case SegmentListItem:
	case SegmentListItemDone:
	case SegmentFileUploadStart:
	case SegmentFileUploadData:
	case SegmentFileUploadEnd:
	case RouteFileUploadStart:
	case RouteFileUploadData:
	case RouteFileUploadEnd:
	case NavigationNewFile:
	case NavigationNewFileDest:
	case NavFileUploadDataStart:
	case NavFileUploadData:
	case NavFileUploadEnd:
		LOG_INF("lezyne_handler: cmd=%u descoped (segments/navigation), ignoring", cmd);
		break;

	default:
		LOG_INF("lezyne_handler: unhandled cmd=%u len=%u", cmd, length);
		break;
	}
}

void lezyne_handler_log_status(void)
{
	/* WRN, not INF: this module is registered at LOG_LEVEL_WRN (see this
	 * file's own top comment), which compiles LOG_INF out entirely --
	 * found on real hardware, "LEZ STATUS" was silently printing nothing
	 * at all despite this function running. This is the one call in the
	 * file that must always be visible, being the whole point of an
	 * on-demand status command. */
	LOG_WRN("lezyne_handler: connected=%d queue=%u/%u fit_download_active=%d remaining=%u",
		m_connected, m_queue_count, LEZ_QUEUE_DEPTH, m_fit_download_active,
		(unsigned)m_fit_remaining);
}
