/*
 * Broadcom 802.11 Message infra (pcie<-> CR4) used for RX offloads
 *
 *
 * Copyright (C) 2012, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: bcm_ol_msg.c chandrum $
 */

#if defined(WLOFFLD) || defined(WLRXOE)

#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <wlioctl.h>
#include <bcmendian.h>
#include <bcmutils.h>
#include <bcm_ol_msg.h>

#define MOD_INC(val, inc, size)  (((val)+(inc))%(size))
#define NEXT(msg) MOD_INC(load32_ua(&msg->wx), 1, load32_ua(&msg->size))
#define store32_ua_1(a, v)	htol32_ua_store_1(v, a)

#ifdef __GNUC__
#define htol32_ua_store_1(val, bytes) ({ \
	uint32 _val = (val); \
	uint8 *_bytes = (uint8 *)(bytes); \
	bcopy(&_val, _bytes, 4); \
})
#else
static INLINE void
htol32_ua_store_1(uint32 val, uint8 *bytes)
{
	bcopy(&val, bytes, 4);
}
#endif

#ifdef BCMDBG
const char *olevent_str[BCM_OL_MSG_MAX] =  {
	"BCM_OL_BEACON_ENABLE",
	"BCM_OL_BEACON_DISABLE",
	"BCM_OL_ARP_ENABLE",
	"BCM_OL_ARP_SETIP",
	"BCM_OL_ARP_DISABLE",
	"BCM_OL_ND_ENABLE",
	"BCM_OL_ND_SETIP",
	"BCM_OL_ND_DISABLE",
	"BCM_OL_RESET",
	"BCM_OL_FIFODEL",
	"BCM_OL_MSG_TEST",
	"BCM_OL_MSG_IE_NOTIFICATION_FLAG",
	"BCM_OL_MSG_IE_NOTIFICATION"
};
#endif /* BCMDBG */

/* for dumping debug output */
#define OLMSG_ERROR_VAL		0x1
#define OLMSG_TRACE_VAL		0x2
#define OLMSG_WARN_VAL		0x4
#define OLMSG_DBG_VAL		0x8

#ifdef BCMDBG
static uint32 olmsg_debug_level = OLMSG_ERROR_VAL |
	OLMSG_TRACE_VAL | OLMSG_DBG_VAL | OLMSG_WARN_VAL;
#define OLMSG_ERROR(args) do { if (olmsg_debug_level & OLMSG_ERROR_VAL) printf args; } while (0)
#define OLMSG_TRACE(args) do { if (olmsg_debug_level & OLMSG_TRACE_VAL) printf args; } while (0)
#define OLMSG_WARN(args) do { if (olmsg_debug_level & OLMSG_WARN_VAL) printf args; } while (0)
#define OLMSG_DBG(args) do { if (olmsg_debug_level & OLMSG_DBG_VAL) printf args; } while (0)
#define ENTER() OLMSG_TRACE(("%s: Enter\n", __FUNCTION__));
#define EXIT()  OLMSG_TRACE(("%s: line (%d) Exit\n", __FUNCTION__, __LINE__));
#else
static uint32 olmsg_debug_level = 0;
#define OLMSG_ERROR(args)
#define OLMSG_DEBUG(args)
#define OLMSG_TRACE(args)
#define OLMSG_WARN(args)
#define OLMSG_DBG(args)
#define ENTER()
#define EXIT()
#endif /* BCMDBG */
/*
 * Create initial partitions; called by the host
 */
int
bcm_olmsg_create(uchar *buf, uint32 len)
{
	olmsg_buf_info  *host_msg;
	olmsg_buf_info  *dngl_msg;

	if (buf == NULL || len == 0 || (len < OLMSG_RW_MAX_ENTRIES*sizeof(olmsg_info))) {
		OLMSG_ERROR(("%s: Invalid arguments\n", __FUNCTION__));
		return BCME_BADARG;
	}

	host_msg = (olmsg_buf_info  *)buf;
	dngl_msg = (olmsg_buf_info  *)(buf + sizeof(olmsg_buf_info));

	store32_ua_1((uint8 *)&host_msg->offset, (OLMSG_RW_MAX_ENTRIES * sizeof(olmsg_buf_info)));
	store32_ua_1((uint8 *)&host_msg->size, OLMSG_HOST_BUF_SZ);
	store32_ua_1((uint8 *)&host_msg->rx, 0);
	store32_ua_1((uint8 *)&host_msg->wx, 0);

	store32_ua_1((uint8 *)&dngl_msg->offset, load32_ua(&host_msg->offset) + OLMSG_HOST_BUF_SZ);
	store32_ua_1((uint8 *)&dngl_msg->size, OLMSG_DGL_BUF_SZ);
	store32_ua_1((uint8 *)&dngl_msg->rx, 0);
	store32_ua_1((uint8 *)&dngl_msg->wx, 0);

	ASSERT((uint32)(load32_ua(&dngl_msg->offset) + load32_ua(&dngl_msg->size)) < len);

	if (((uint32)(load32_ua(&dngl_msg->offset) + load32_ua(&dngl_msg->size))) > len) {
		OLMSG_ERROR(("%s: Invalid parameters, actual buffer size (%d) is less "
			"than total length of partitions (%d) \n", __FUNCTION__, len,
			(load32_ua(&dngl_msg->offset) + load32_ua(&dngl_msg->size))));
		return BCME_BUFTOOSHORT;
	}
	return BCME_OK;
}

/*
 * Assumption: bcm_olmsg_create is already called
 * Setup read/write pointers
 */
int
bcm_olmsg_init(olmsg_info *ol_info, uchar *buf, uint32 len, uint8 rx, uint8 wx)
{

	/* sanity checks */
	if (ol_info == NULL || buf == NULL || len == 0) {
		OLMSG_ERROR(("%s: Invalid arguments\n", __FUNCTION__));
		return BCME_BADARG;
	}
	ol_info->msg_buff = buf;
	ol_info->len = len;

	ol_info->write = (olmsg_buf_info *)((ol_info->msg_buff) + wx * sizeof(olmsg_buf_info));
	ol_info->read = (olmsg_buf_info *)((ol_info->msg_buff) + rx * sizeof(olmsg_buf_info));
	ol_info->next_seq = 0;

	if (olmsg_debug_level & OLMSG_DBG_VAL)
		bcm_olmsg_dump_record(ol_info);

	return BCME_OK;
}

/*
 * Free up the allocations
 */
void
bcm_olmsg_deinit(olmsg_info *ol_info)
{
	if (ol_info == NULL) {
		return;
	}
	bzero(ol_info->msg_buff, OLMSG_RW_MAX_ENTRIES * sizeof(olmsg_buf_info));
	ol_info->write = ol_info->read = NULL;
}

/*
 * Return the free buffer space.
 * 1 byte is used to detect circular buffer full case
 */
uint32 bcm_olmsg_avail(olmsg_info *ol)
{
	uint32 avail = 0;
	olmsg_buf_info *buf_info = ol->write;
	uint32 w_rx = load32_ua(&buf_info->rx);
	uint32 w_wx = load32_ua(&buf_info->wx);
	uint32 w_size = load32_ua(&buf_info->size);

	if (w_rx <= w_wx) {
		avail = (w_size - w_wx + w_rx) - 1;
	}
	else {
		avail = (w_rx -w_wx) - 1;
	}

	OLMSG_DBG(("%s: size 0x%x rx 0x%x wx 0x%x\n", __FUNCTION__, w_size, w_wx, w_rx));
	OLMSG_TRACE(("%s: free space 0x%x \n", __FUNCTION__, avail));
	return avail;
}
/*
 * Returns true if there the write buffer space is full
 */
bool bcm_olmsg_is_writebuf_full(olmsg_info *ol_info)
{
	olmsg_buf_info *buf_info = ol_info->write;
	OLMSG_DBG(("rx 0x%x mod(wx) 0x%x \n", load32_ua(&buf_info->rx), NEXT(buf_info)));
	return NEXT(buf_info) == load32_ua(&buf_info->rx);
}

/* write a message to the shared buffer
 * return if there is no free space
 * TBD: Wait for Xms if there is no space. return failure if still cann't find space
 */
int
bcm_olmsg_writemsg(olmsg_info *ol, uchar *buf, uint16 len)
{
	olmsg_header *hdr = (olmsg_header *)buf;
	olmsg_buf_info *buf_info = ol->write;
	uint32 copy_bytes;
	uint32 w_wx = load32_ua(&buf_info->wx);
	uint32 w_size = load32_ua(&buf_info->size);
	uchar *dst = ol->msg_buff + load32_ua(&buf_info->offset);

	if (olmsg_debug_level & OLMSG_DBG_VAL) {
		OLMSG_DBG(("Dumping r/w records before writing a message\n"));
		bcm_olmsg_dump_record(ol);
	}
	if (bcm_olmsg_avail(ol) < len) {
		OLMSG_ERROR(("Insuffcient space to write message: Available 0x%x required 0x%x\n",
			bcm_olmsg_avail(ol), len));
		return BCME_NORESOURCE;
	}
	hdr->seq = ol->next_seq++;

	if (olmsg_debug_level & OLMSG_DBG_VAL) {
		OLMSG_DBG(("Writing message len %d write_ptr %p\n", len, buf_info));
		bcm_olmsg_dump_msg(ol, hdr);
	}
	copy_bytes = MIN(w_size - w_wx, len);

	if (copy_bytes) {
		OLMSG_DBG(("Copying %d of %d bytes \n", copy_bytes, len));
		bcopy(buf, dst + w_wx, copy_bytes);
		w_wx = MOD_INC(w_wx, copy_bytes, w_size);
	}
	if (len > copy_bytes) {
		OLMSG_DBG(("Copying %d of %d bytes \n", len-copy_bytes, len));
		bcopy(buf+copy_bytes, dst + w_wx, len-copy_bytes);
		w_wx = MOD_INC(w_wx, (len-copy_bytes), w_size);
	}

	store32_ua_1((uint8 *)&buf_info->wx, w_wx);

	if (olmsg_debug_level & OLMSG_DBG_VAL) {
		OLMSG_DBG(("Dumping r/w records after writing a message\n"));
		bcm_olmsg_dump_record(ol);
	}
	return BCME_OK;
}

/*
 * Returns number of bytes yet to be read
 * is this equal to (total size - available bytes) ?
 */
uint32 bcm_olmsg_bytes_to_read(olmsg_info *ol_info)
{
	uint32 read_bytes;
	olmsg_buf_info *buf_info = ol_info->read;
	uint32 r_wx = load32_ua(&buf_info->wx), r_size = load32_ua(&buf_info->size);
	uint32 r_rx = load32_ua(&buf_info->rx);
	if (r_wx >= r_rx) {
		read_bytes = r_wx - r_rx;
	}
	else {
		read_bytes = r_size - r_rx + r_wx;
	}
	OLMSG_TRACE(("Bytes yet to be read %d\n", read_bytes));
	return read_bytes;
}
/*
 * Returns TRUE of the buffer is empty. Else FALSE
 */
bool bcm_olmsg_is_readbuf_empty(olmsg_info *ol_info)
{
	olmsg_buf_info *buf_info = ol_info->read;
	OLMSG_TRACE(("wx 0x%x rx 0x%x\n", load32_ua(&buf_info->wx), load32_ua(&buf_info->rx)));
	return load32_ua(&buf_info->wx) == load32_ua(&buf_info->rx);
}
/*
 * Read "len"/remaining bytes to dst from read buffer.
 * Returns number bytes copied to dst.
 * Does not increment rx though
 */
uint32
bcm_olmsg_peekbytes(olmsg_info *ol, uchar *dst, uint32 len)
{
	uint32 read_bytes = 0;
	olmsg_buf_info *buf_info = ol->read;
	uint32 r_wx = load32_ua(&buf_info->wx), r_size = load32_ua(&buf_info->size);
	uint32 r_rx = load32_ua(&buf_info->rx);
	uchar *src = ol->msg_buff + load32_ua(&buf_info->offset);
	OLMSG_DBG(("wx 0x%x rx 0x%x \n", r_wx, r_rx));

	if (bcm_olmsg_is_readbuf_empty(ol)) {
		OLMSG_ERROR((" Read buffer is empty (rx 0x%x wx 0x%x\n", r_wx, r_rx));
		return 0;
	}
	/* number of bytes to be read */
	read_bytes = MIN(bcm_olmsg_bytes_to_read(ol), len);

	OLMSG_DBG(("Reading %d bytes \n", read_bytes));
	if (r_rx < r_wx)
		bcopy(src+r_rx, dst, read_bytes);
	else {
		uint32 temp_r = r_size - r_rx;
		ASSERT(read_bytes > (buf_info->size - buf_info->rx));
		bcopy(src+r_rx, dst, temp_r);
		bcopy(src, dst+temp_r, read_bytes - temp_r);
	}
	return read_bytes;
}
/*
 * Same as peek bytes but increments rx as well.
 */
uint32
bcm_olmsg_readbytes(olmsg_info *ol, uchar *dst, uint32 len)
{
	uint32 read_bytes = 0;
	olmsg_buf_info *buf_info = ol->read;
	uint32 r_rx = load32_ua(&buf_info->rx), r_size = load32_ua(&buf_info->size);

	if (olmsg_debug_level & OLMSG_DBG_VAL) {
		OLMSG_DBG(("Dumping r/w records before reading a message\n"));
		bcm_olmsg_dump_record(ol);
	}

	read_bytes = bcm_olmsg_peekbytes(ol, dst, len);
	r_rx = MOD_INC(r_rx, read_bytes, r_size);
	store32_ua_1((uint8 *)&buf_info->rx, r_rx);
	if (olmsg_debug_level & OLMSG_DBG_VAL) {
		OLMSG_DBG(("Dumping r/w records after reading a message\n"));
		bcm_olmsg_dump_record(ol);
	}

	return read_bytes;

}
/*
 * Get the message length.
 */
uint16
bcm_olmsg_peekmsg_len(olmsg_info *ol)
{
	olmsg_header hdr;
	uint16 msg_size = 0;
	uint32 read_bytes = bcm_olmsg_bytes_to_read(ol);
	/* do we have atleast sizeof(olmsg_header) bytes in the buffer */
	if (read_bytes >= sizeof(olmsg_header)) {
		bcm_olmsg_peekbytes(ol, (uchar *)(&hdr), sizeof(olmsg_header));
		msg_size = hdr.len + sizeof(olmsg_header);
	}
	OLMSG_TRACE(("Message len 0x%x\n", msg_size));
	return msg_size;
}
/*
 * Read the next message to buf
 */
uint16
bcm_olmsg_readmsg(olmsg_info *ol, uchar *buf, uint16 len)
{
	uint16 msg_len = bcm_olmsg_peekmsg_len(ol);
	if (!msg_len || (len < msg_len) || (buf == NULL)) {
		OLMSG_ERROR(("Error in reading the message of size(1) %d\n", msg_len));
		return 0;
	}
	/* Read the message */
	if (bcm_olmsg_readbytes(ol, buf, msg_len) != msg_len) {
		OLMSG_ERROR(("Error in reading the message of size(2) %d\n", msg_len));
		return 0;
	}

	if (olmsg_debug_level & OLMSG_DBG_VAL) {
		OLMSG_DBG(("Dumping the read message \n"));
		bcm_olmsg_dump_msg(ol, (olmsg_header *)buf);
	}
	return msg_len;
}
/*
 * Dump functions
 */
void bcm_olmsg_dump_msg(olmsg_info *ol, olmsg_header *hdr)
{

	OLMSG_ERROR(("Offload Message: \n"));
	if (hdr->type < BCM_OL_MSG_MAX)
		OLMSG_ERROR(("	Type: %s\n", olevent_str[hdr->type]));
	else
		OLMSG_ERROR(("	Type: %d is invalid \n", hdr->type));
	OLMSG_ERROR(("	Sequence:%d\n", hdr->seq));
	OLMSG_ERROR(("	len:%d\n", hdr->len));
}
void bcm_olmsg_dump_record(olmsg_info *ol)
{
#ifdef BCMDBG
	olmsg_buf_info *buf_write = ol->write;
	olmsg_buf_info *buf_read = ol->read;
#endif
	OLMSG_ERROR(("Dumping init values\n"));
	OLMSG_ERROR(("	msgbuff %p len:0%x write_ptr %p read_ptr %p\n",
		ol->msg_buff, ol->len, buf_write, buf_read));

	OLMSG_ERROR(("Dumping write record \n"));
	OLMSG_ERROR(("	offset 0x%08x size 0x%08x rx 0x%08x wx %08x\n",
		load32_ua(&buf_write->offset), load32_ua(&buf_write->size),
		load32_ua(&buf_write->rx), load32_ua(&buf_write->wx)));

	OLMSG_ERROR(("Dumping read record \n"));
	OLMSG_ERROR(("	offset 0x%08x size 0x%08x rx 0x%08x wx %08x\n",
		load32_ua(&buf_read->offset), load32_ua(&buf_read->size),
		load32_ua(&buf_read->rx), load32_ua(&buf_read->wx)));
}

#endif /* WLOFFLD */
