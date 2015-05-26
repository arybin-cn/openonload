/*
** Copyright 2005-2015  Solarflare Communications Inc.
**                      7505 Irvine Center Drive, Irvine, CA 92618, USA
** Copyright 2002-2005  Level 5 Networks Inc.
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of version 2 of the GNU General Public License as
** published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2015 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/* Common definitions for all Efx net driver code */

#ifndef EFX_NET_DRIVER_H
#define EFX_NET_DRIVER_H

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/timer.h>
#ifndef EFX_USE_KCOMPAT
#include <linux/mdio.h>
#endif
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/device.h>
#ifndef EFX_USE_KCOMPAT
#include <linux/highmem.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#endif
#include <linux/rwsem.h>
#include <linux/vmalloc.h>
#include <linux/i2c.h>
#ifndef EFX_USE_KCOMPAT
#include <linux/mtd/mtd.h>
#else
#include "linux_mtd_mtd.h"
#endif
#ifndef EFX_USE_KCOMPAT
#include <net/busy_poll.h>
#endif


#ifdef EFX_NOT_UPSTREAM
#include "config.h"
#endif

#ifdef EFX_USE_KCOMPAT
/* Must come before other headers */
#include "kernel_compat.h"
#endif

#if defined(EFX_USE_KCOMPAT) && defined(EFX_HAVE_BUSY_POLL)
#include <net/busy_poll.h>
#endif

#include "enum.h"
#include "bitfield.h"
#include "driverlink_api.h"
#include "driverlink.h"

/**************************************************************************
 *
 * Build definitions
 *
 **************************************************************************/

#define EFX_DRIVER_VERSION	"4.4.1.1021"

#ifdef DEBUG
#define EFX_BUG_ON_PARANOID(x) BUG_ON(x)
#define EFX_WARN_ON_PARANOID(x) WARN_ON(x)
#else
#define EFX_BUG_ON_PARANOID(x) do {} while (0)
#define EFX_WARN_ON_PARANOID(x) do {} while (0)
#endif

#if defined(EFX_NOT_UPSTREAM)

#ifndef DEBUG
#define EFX_FATAL netif_err
#else
/* fatal error that cannot be ignored */
#define EFX_FATAL(efx, type, netdev, fmt, args...) do {		\
	netif_err(efx, type, netdev, "FTL: " fmt, ##args);	\
	efx_schedule_reset(efx, RESET_TYPE_DISABLE);		\
} while (0)
#endif

#endif

#if defined(EFX_NOT_UPSTREAM) && !defined(__VMKLNX__)
#define EFX_RX_PAGE_SHARE	1
#ifdef EFX_HAVE_VLAN_RX_PATH
#define EFX_USE_FAKE_VLAN_RX_ACCEL 1
#endif
#define EFX_USE_FAKE_VLAN_TX_ACCEL 1
#endif

#if !defined(EFX_HAVE_NDO_SELECT_QUEUE) || !defined(EFX_HAVE_SKB_TX_HASH) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
#undef EFX_ENABLE_SFC_XPS
#endif

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_ENABLE_SARFS) && (defined(CONFIG_XPS) || defined(EFX_ENABLE_SFC_XPS))
#define EFX_USE_SARFS
#endif

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_ENABLE_MCDI_PROXY_AUTH)
#define EFX_USE_MCDI_PROXY_AUTH
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)) && defined(CONFIG_NET_NS)
#define EFX_USE_MCDI_PROXY_AUTH_NL
#endif
#endif

/**************************************************************************
 *
 * Efx data structures
 *
 **************************************************************************/
void ci_frc64(uint64_t* pval);

#define EFX_MAX_CHANNELS 32U
#define EFX_MAX_RX_QUEUES EFX_MAX_CHANNELS
#define EFX_EXTRA_CHANNEL_IOV	0
#define EFX_EXTRA_CHANNEL_PTP	1
#define EFX_MAX_EXTRA_CHANNELS	2U

/* Checksum generation is a per-queue option in hardware, so each
 * queue visible to the networking core is backed by two hardware TX
 * queues. */
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_USE_TX_MQ)
#define EFX_MAX_CORE_TX_QUEUES	EFX_MAX_CHANNELS
#else
#define EFX_MAX_CORE_TX_QUEUES	1U
#endif
#define EFX_TXQ_TYPE_OFFLOAD	1
#define EFX_TXQ_TYPES		2
#define EFX_MAX_TX_QUEUES	(EFX_TXQ_TYPES * EFX_MAX_CORE_TX_QUEUES)

/* Maximum possible MTU the driver supports */
#define EFX_MAX_MTU (9 * 1024)

#if !defined(EFX_NOT_UPSTREAM) || defined(EFX_RX_PAGE_SHARE)
/* Size of an RX scatter buffer.  Small enough to pack 2 into a 4K page,
 * and should be a multiple of the cache line size.
 */
#define EFX_RX_USR_BUF_SIZE	(2048 - 256)
#else
/* Size of an RX scatter buffer. Page can't be shared on ESX, so use
 * maximum aligned to cache line size.
 */
#define EFX_RX_USR_BUF_SIZE	(PAGE_SIZE - L1_CACHE_BYTES)
#endif

/* If possible, we should ensure cache line alignment at start and end
 * of every buffer.  Otherwise, we just need to ensure 4-byte
 * alignment of the network header.
 */
#if NET_IP_ALIGN == 0
#define EFX_RX_BUF_ALIGNMENT	L1_CACHE_BYTES
#else
#define EFX_RX_BUF_ALIGNMENT	4
#endif

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_WITH_VMWARE_NETQ)
/* Forward declare structures used in the VMWare netq support. */
struct netq_sw_filter;
struct netq_hw_filter;
#endif

/* Forward declare Precision Time Protocol (PTP) support structure. */
struct efx_ptp_data;
struct hwtstamp_config;

#if defined(EFX_NOT_UPSTREAM) && defined(CONFIG_SFC_AOE)
/* Forward declare structure used in the AOE support */
struct efx_aoe_data;
#endif

struct efx_self_tests;

enum efx_rss_mode {
	EFX_RSS_PACKAGES,
	EFX_RSS_CORES,
	EFX_RSS_HYPERTHREADS,
	EFX_RSS_CUSTOM,
};

/**
 * struct efx_buffer - A general-purpose DMA buffer
 * @addr: host base address of the buffer
 * @dma_addr: DMA base address of the buffer
 * @len: Buffer length, in bytes
 *
 * The NIC uses these buffers for its interrupt status registers and
 * MAC stats dumps.
 */
struct efx_buffer {
	void *addr;
	dma_addr_t dma_addr;
	unsigned int len;
};

/**
 * struct efx_special_buffer - DMA buffer entered into buffer table
 * @buf: Standard &struct efx_buffer
 * @index: Buffer index within controller;s buffer table
 * @entries: Number of buffer table entries
 *
 * The NIC has a buffer table that maps buffers of size %EFX_BUF_SIZE.
 * Event and descriptor rings are addressed via one or more buffer
 * table entries (and so can be physically non-contiguous, although we
 * currently do not take advantage of that).  On Falcon and Siena we
 * have to take care of allocating and initialising the entries
 * ourselves.  On later hardware this is managed by the firmware and
 * @index and @entries are left as 0.
 */
struct efx_special_buffer {
	struct efx_buffer buf;
	unsigned int index;
	unsigned int entries;
};

/**
 * struct efx_tx_buffer - buffer state for a TX descriptor
 * @skb: When @flags & %EFX_TX_BUF_SKB, the associated socket buffer to be
 *	freed when descriptor completes
 * @heap_buf: When @flags & %EFX_TX_BUF_HEAP, the associated heap buffer to be
 *	freed when descriptor completes.
 * @option: When @flags & %EFX_TX_BUF_OPTION, a NIC-specific option descriptor.
 * @dma_addr: DMA address of the fragment.
 * @flags: Flags for allocation and DMA mapping type
 * @len: Length of this fragment.
 *	This field is zero when the queue slot is empty.
 * @unmap_len: Length of this fragment to unmap
 * @dma_offset: Offset of @dma_addr from the address of the backing DMA mapping.
 * Only valid if @unmap_len != 0.
 */
struct efx_tx_buffer {
	union {
		const struct sk_buff *skb;
		void *heap_buf;
	};
	union {
		efx_qword_t option;
		dma_addr_t dma_addr;
	};
	unsigned short flags;
	unsigned short len;
	unsigned short unmap_len;
	unsigned short dma_offset;
};
#define EFX_TX_BUF_CONT		1	/* not last descriptor of packet */
#define EFX_TX_BUF_SKB		2	/* buffer is last part of skb */
#define EFX_TX_BUF_HEAP		4	/* buffer was allocated with kmalloc() */
#define EFX_TX_BUF_MAP_SINGLE	8	/* buffer was mapped with dma_map_single() */
#define EFX_TX_BUF_OPTION	0x10	/* empty buffer for option descriptor */

/**
 * struct efx_tx_queue - An Efx TX queue
 *
 * This is a ring buffer of TX fragments.
 * Since the TX completion path always executes on the same
 * CPU and the xmit path can operate on different CPUs,
 * performance is increased by ensuring that the completion
 * path and the xmit path operate on different cache lines.
 * This is particularly important if the xmit path is always
 * executing on one CPU which is different from the completion
 * path.  There is also a cache line for members which are
 * read but not written on the fast path.
 *
 * @efx: The associated Efx NIC
 * @queue: DMA queue number
 * @channel: The associated channel
 * @core_txq: The networking core TX queue structure
 * @buffer: The software buffer ring
 * @cb_page: Array of pages of copy buffers
 * @txd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @piobuf: PIO buffer region for this TX queue (shared with its partner).
 *	Size of the region is efx_piobuf_size.
 * @piobuf_offset: Buffer offset to be specified in PIO descriptors
 * @read_count: Current read pointer.
 *	This is the number of buffers that have been removed from both rings.
 * @old_write_count: The value of @write_count when last checked.
 *	This is here for performance reasons.  The xmit path will
 *	only get the up-to-date value of @write_count if this
 *	variable indicates that the queue is empty.  This is to
 *	avoid cache-line ping-pong between the xmit path and the
 *	completion path.
 * @merge_events: Number of TX merged completion events
 * @insert_count: Current insert pointer
 *	This is the number of buffers that have been added to the
 *	software ring.
 * @write_count: Current write pointer
 *	This is the number of buffers that have been added to the
 *	hardware ring.
 * @packet_write_count: Completable write pointer
 *	This is the write pointer of the last packet written.
 *	Normally this will equal @write_count, but as option descriptors
 *	don't produce completion events, they won't update this.
 *	Filled in iff @efx->type->option_descriptors; only used for PIO.
 *	Thus, this is written and used on EF10, and neither on farch.
 * @old_read_count: The value of read_count when last checked.
 *	This is here for performance reasons.  The xmit path will
 *	only get the up-to-date value of read_count if this
 *	variable indicates that the queue is full.  This is to
 *	avoid cache-line ping-pong between the xmit path and the
 *	completion path.
 * @tso_bursts: Number of times TSO xmit invoked by kernel
 * @tso_long_headers: Number of packets with headers too long for standard
 *	blocks
 * @tso_packets: Number of packets via the TSO xmit path
 * @pushes: Number of times the TX push feature has been used
 * @pio_packets: Number of times the TX PIO feature has been used
 * @notify_count: Count of notified descriptors to the NIC
 * @sarfs_sample_count: Number of TCP packets since we last inspected
 *	one for SARFS.
 * @sarfs_update: Number of updates to SARFS filter requested from this queue
 * @empty_read_count: If the completion path has seen the queue as empty
 *	and the transmission path has not yet checked this, the value of
 *	@read_count bitwise-added to %EFX_EMPTY_COUNT_VALID; otherwise 0.
 */
struct efx_tx_queue {
	/* Members which don't change on the fast path */
	struct efx_nic *efx ____cacheline_aligned_in_smp;
	unsigned queue;
	struct efx_channel *channel;
	struct netdev_queue *core_txq;
	struct efx_tx_buffer *buffer;
	struct efx_buffer *cb_page;
	struct efx_special_buffer txd;
	unsigned int ptr_mask;
	void __iomem *piobuf;
	unsigned int piobuf_offset;
	bool tx_coalesce_doorbell;
#ifdef CONFIG_SFC_DEBUGFS
	struct dentry *debug_dir;
#endif

	/* Members used mainly on the completion path */
	unsigned int read_count ____cacheline_aligned_in_smp;
	unsigned int old_write_count;
	unsigned int merge_events;
	unsigned int doorbell_notify_comp;

	/* Members used only on the xmit path */
	unsigned int insert_count ____cacheline_aligned_in_smp;
	unsigned int write_count;
	unsigned int packet_write_count;
	unsigned int old_read_count;
	unsigned int tso_bursts;
	unsigned int tso_long_headers;
	unsigned int tso_packets;
	unsigned int pushes;
	unsigned int pio_packets;
	unsigned int doorbell_notify_tx;
	unsigned int notify_count;
#if defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_SARFS)
	unsigned sarfs_sample_count;
#ifdef CONFIG_SFC_DEBUGFS
	unsigned sarfs_update;
#endif
#endif
	/* Statistics to supplement MAC stats */
#ifdef EFX_NOT_UPSTREAM
	u64 tx_bytes;
#endif /* EFX_NOT_UPSTREAM */
	unsigned long tx_packets;

	/* Members shared between paths and sometimes updated */
	unsigned int empty_read_count ____cacheline_aligned_in_smp;
#define EFX_EMPTY_COUNT_VALID 0x80000000
	atomic_t flush_outstanding;
	uint64_t last_tx_notify_tstamp;
	unsigned int last_tx_notify_write_count;
	unsigned int last_tx_notify_push;
	uint64_t last_tx_comp_tstamp;
	unsigned int last_tx_comp_index;
};

/**
 * struct efx_rx_buffer - An Efx RX data buffer
 * @dma_addr: DMA base address of the buffer
 * @page: The associated page buffer.
 *	Will be %NULL if the buffer slot is currently free.
 * @page_offset: If pending: offset in @page of DMA base address.
 *	If completed: offset in @page of Ethernet header.
 * @len: If pending: length for DMA descriptor.
 *	If completed: received length, excluding hash prefix.
 * @flags: Flags for buffer and packet state.  These are only set on the
 *	first buffer of a scattered packet.
 */
struct efx_rx_buffer {
	dma_addr_t dma_addr;
	struct page *page;
	u16 page_offset;
	u16 len;
	u16 flags;
#if defined(EFX_NOT_UPSTREAM)
	/* VLAN tag in host byte order.  Iff the EFX_RX_PKT_VLAN_XTAG
	 * flag is set, the tag has been moved here. If EFX_USE_FAKE_VLAN_RX_ACCEL
	 * is not defined that the tag is copied here.
	 */
	u16 vlan_tci;
#define EFX_RX_BUF_VLAN_XTAG	0x8000
#endif
};
#define EFX_RX_BUF_LAST_IN_PAGE		0x0001
#define EFX_RX_PKT_CSUMMED		0x0002
#define EFX_RX_PKT_DISCARD		0x0004
#define EFX_RX_PKT_VLAN			0x0008
#define EFX_RX_PKT_IPV4			0x0010
#define EFX_RX_PKT_IPV6			0x0020
#define EFX_RX_PKT_TCP			0x0040
#define EFX_RX_PKT_PREFIX_LEN		0x0080	/* length is in prefix only */
#define EFX_RX_PAGE_IN_RECYCLE_RING	0x0100

/**
 * struct efx_rx_page_state - Page-based rx buffer state
 *
 * Inserted at the start of every page allocated for receive buffers.
 * Used to facilitate sharing dma mappings between recycled rx buffers
 * and those passed up to the kernel.
 *
 * @dma_addr: The dma address of this page.
 */
struct efx_rx_page_state {
	dma_addr_t dma_addr;

	unsigned int __pad[0] ____cacheline_aligned;
};

/**
 * struct efx_rx_queue - An Efx RX queue
 * @efx: The associated Efx NIC
 * @core_index: Index of network core RX queue.  Will be >= 0 iff this
 *	is associated with a real RX queue.
 * @buffer: The software buffer ring
 * @rxd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @refill_enabled: Enable refill whenever fill level is low
 * @flush_pending: Set when a RX flush is pending. Has the same lifetime as
 *	@rxq_flush_pending.
 * @added_count: Number of buffers added to the receive queue.
 * @notified_count: Number of buffers given to NIC (<= @added_count).
 * @removed_count: Number of buffers removed from the receive queue.
 * @scatter_n: Used by NIC specific receive code.
 * @scatter_len: Used by NIC specific receive code.
 * @page_ring: The ring to store DMA mapped pages for reuse.
 * @page_add: Counter to calculate the write pointer for the recycle ring.
 * @page_remove: Counter to calculate the read pointer for the recycle ring.
 * @page_recycle_count: The number of pages that have been recycled.
 * @page_recycle_failed: The number of pages that couldn't be recycled because
 *      the kernel still held a reference to them.
 * @page_recycle_full: The number of pages that were released because the
 *      recycle ring was full.
 * @page_ptr_mask: The number of pages in the RX recycle ring minus 1.
 * @max_fill: RX descriptor maximum fill level (<= ring size)
 * @fast_fill_trigger: RX descriptor fill level that will trigger a fast fill
 *	(<= @max_fill)
 * @min_fill: RX descriptor minimum non-zero fill level.
 *	This records the minimum fill level observed when a ring
 *	refill was triggered.
 * @recycle_count: RX buffer recycle counter.
 * @slow_fill: Timer used to defer efx_nic_generate_fill_event().
 */
struct efx_rx_queue {
	struct efx_nic *efx;
	int core_index;
	struct efx_rx_buffer *buffer;
	struct efx_special_buffer rxd;
	unsigned int ptr_mask;
	bool refill_enabled;
	bool flush_pending;

	unsigned int added_count;
	unsigned int notified_count;
	unsigned int removed_count;
	unsigned int scatter_n;
	unsigned int scatter_len;
#if !defined(EFX_NOT_UPSTREAM) || defined(EFX_RX_PAGE_SHARE)
	struct page **page_ring;
	unsigned int page_add;
	unsigned int page_remove;
	unsigned int page_recycle_count;
	unsigned int page_recycle_failed;
	unsigned int page_recycle_full;
	unsigned int page_repost_count;
	unsigned int page_ptr_mask;
#endif
	unsigned int max_fill;
	unsigned int fast_fill_trigger;
	unsigned int min_fill;
	unsigned int min_overfill;
	unsigned int recycle_count;
	struct timer_list slow_fill;
	unsigned int slow_fill_count;
	unsigned int failed_flush_count;
	/* Statistics to supplement MAC stats */
	unsigned long rx_packets;

#define SKB_CACHE_SIZE 8
	struct sk_buff *skb_cache[SKB_CACHE_SIZE];
	unsigned skb_cache_next_unused;

#ifdef CONFIG_SFC_DEBUGFS
	struct dentry *debug_dir;
#endif
};

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_SFC_LRO)

/**
 * struct efx_ssr_conn - Connection state for Soft Segment Reassembly (SSR) aka LRO
 * @link: Link for hash table and free list.
 * @active_link: Link for active_conns list
 * @l2_id: Identifying information from layer 2
 * @conn_hash: Hash of connection 4-tuple
 * @source: Source TCP port number
 * @dest: Destination TCP port number
 * @n_in_order_pkts: Number of in-order packets with payload.
 * @next_seq: Next in-order sequence number.
 * @last_pkt_jiffies: Time we last saw a packet on this connection.
 * @skb: The SKB we are currently holding.
 *	If %NULL, then all following fields are undefined.
 * @skb_tail: The tail of the frag_list of SKBs we're holding.
 *	Only valid after at least one merge (and when not in page-mode).
 * @th_last: The TCP header of the last packet merged.
 * @next_buf: The next RX buffer to process.
 * @next_eh: Ethernet header of the next buffer.
 * @next_iph: IP header of the next buffer.
 * @delivered: True if we've delivered a payload packet up this interrupt.
 * @sum_len: IPv4 tot_len or IPv6 payload_len for @skb.
 */
struct efx_ssr_conn {
	struct list_head link;
	struct list_head active_link;
	u32 l2_id;
	u32 conn_hash;
	__be16 source, dest;
	int n_in_order_pkts;
	unsigned next_seq;
	unsigned last_pkt_jiffies;
	struct sk_buff *skb;
	struct sk_buff *skb_tail;
	struct tcphdr *th_last;
	struct efx_rx_buffer next_buf;
	char *next_eh;
	void *next_iph;
	int delivered;
	u16 sum_len;
};

/**
 * struct efx_ssr_state - Port state for Soft Segment Reassembly (SSR) aka LRO
 * @efx: The associated NIC.
 * @conns_mask: Number of hash buckets - 1.
 * @conns: Hash buckets for tracked connections.
 * @conns_n: Length of linked list for each hash bucket.
 * @active_conns: Connections that are holding a packet.
 *	Connections are self-linked when not in this list.
 * @free_conns: Free efx_ssr_conn instances.
 * @last_purge_jiffies: The value of jiffies last time we purged idle
 *	connections.
 * @n_merges: Number of packets absorbed by SSR.
 * @n_bursts: Number of bursts spotted by SSR.
 * @n_slow_start: Number of packets not merged because connection may be in
 *	slow-start.
 * @n_misorder: Number of out-of-order packets seen in tracked streams.
 * @n_too_many: Incremented when we're trying to track too many streams.
 * @n_new_stream: Number of distinct streams we've tracked.
 * @n_drop_idle: Number of streams discarded because they went idle.
 * @n_drop_closed: Number of streams that have seen a FIN or RST.
 */
struct efx_ssr_state {
	struct efx_nic *efx;
	unsigned conns_mask;
	struct list_head *conns;
	unsigned *conns_n;
	struct list_head active_conns;
	struct list_head free_conns;
	unsigned last_purge_jiffies;
	unsigned n_merges;
	unsigned n_bursts;
	unsigned n_slow_start;
	unsigned n_misorder;
	unsigned n_too_many;
	unsigned n_new_stream;
	unsigned n_drop_idle;
	unsigned n_drop_closed;
};

#endif

#ifdef CONFIG_SFC_PTP
enum efx_sync_events_state {
	SYNC_EVENTS_DISABLED = 0,
	SYNC_EVENTS_QUIESCENT,
	SYNC_EVENTS_REQUESTED,
	SYNC_EVENTS_VALID,
};
#endif

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_SARFS)
struct efx_sarfs_key {
	__be32 laddr;
	__be32 raddr;
	__be16 lport;
	__be16 rport;
};

struct efx_sarfs_entry {
	struct efx_sarfs_key key;

	unsigned long last_modified;
	bool filter_inserted;
	bool problem;
	int queue;
	u32 filter_id;
};

struct efx_sarfs_state {
	int enabled;
	unsigned conns_mask;
	struct efx_sarfs_entry *conns;
	unsigned long last_modified;
	spinlock_t lock;
};
#endif

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_WITH_VMWARE_NETQ)
/* VMware netqueue use flags */
#define NETQ_USE_DEFAULT	(0U)
#define NETQ_USE_RX		(1U)
#define NETQ_USE_TX		(2U)
#define NETQ_USE_RSS		(4U)
#define NETQ_USE_LRO		(8U)
#endif

/**
 * struct efx_channel - An Efx channel
 *
 * A channel comprises an event queue, at least one TX queue, at least
 * one RX queue, and an associated tasklet for processing the event
 * queue.
 *
 * @efx: Associated Efx NIC
 * @channel: Channel instance number
 * @type: Channel type definition
 * @eventq_init: Event queue initialised flag
 * @enabled: Channel enabled indicator
 * @tx_coalesce_doorbell: Coalescing of doorbell notifications enabled
 *      for this channel
 * @holdoff_doorbell: Flag indicating that the channel is being processed
 * @irq: IRQ number (MSI and MSI-X only)
 * @irq_moderation: IRQ moderation value (in hardware ticks)
 * @napi_dev: Net device used with NAPI
 * @napi_str: NAPI control structure
 * @state: state for NAPI vs busy polling
 * @state_lock: lock protecting @state
 * @eventq: Event queue buffer
 * @eventq_mask: Event queue pointer mask
 * @eventq_read_ptr: Event queue read pointer
 * @event_test_cpu: Last CPU to handle interrupt or test event for this channel
 * @irq_count: Number of IRQs since last adaptive moderation decision
 * @irq_mod_score: IRQ moderation score
 * @n_rx_tobe_disc: Count of RX_TOBE_DISC errors
 * @n_rx_ip_hdr_chksum_err: Count of RX IP header checksum errors
 * @n_rx_tcp_udp_chksum_err: Count of RX TCP and UDP checksum errors
 * @n_rx_eth_crc_err: Count of RX CRC errors
 * @n_rx_mcast_mismatch: Count of unmatched multicast frames
 * @n_rx_frm_trunc: Count of RX_FRM_TRUNC errors
 * @n_rx_overlength: Count of RX_OVERLENGTH errors
 * @n_skbuff_leaks: Count of skbuffs leaked due to RX overrun
 * @n_rx_nodesc_trunc: Number of RX packets truncated and then dropped due to
 *	lack of descriptors
 * @n_rx_merge_events: Number of RX merged completion events
 * @n_rx_merge_packets: Number of RX packets completed by merged events
 * @rx_pkt_n_frags: Number of fragments in next packet to be delivered by
 *	__efx_rx_packet(), or zero if there is none
 * @rx_pkt_index: Ring index of first buffer for next packet to be delivered
 *	by __efx_rx_packet(), if @rx_pkt_n_frags != 0
 * @rx_queue: RX queue for this channel
 * @tx_queue: TX queues for this channel
 * @sync_events_state: Current state of sync events on this channel
 * @sync_timestamp_major: Major part of the last ptp sync event
 * @sync_timestamp_minor: Minor part of the last ptp sync event
 */
struct efx_channel {
	struct efx_nic *efx;
	int channel;
	const struct efx_channel_type *type;
	bool eventq_init;
	bool enabled;
	bool tx_coalesce_doorbell;
	bool holdoff_doorbell;
	int irq;
	unsigned int irq_moderation;
	struct net_device *napi_dev;
	struct napi_struct napi_str;
#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned int state;
	spinlock_t state_lock;
#define EFX_CHANNEL_STATE_IDLE		0
#define EFX_CHANNEL_STATE_NAPI		(1 << 0)  /* NAPI owns this channel */
#define EFX_CHANNEL_STATE_POLL		(1 << 1)  /* poll owns this channel */
#define EFX_CHANNEL_STATE_DISABLED	(1 << 2)  /* channel is disabled */
#define EFX_CHANNEL_STATE_NAPI_YIELD	(1 << 3)  /* NAPI yielded this channel */
#define EFX_CHANNEL_STATE_POLL_YIELD	(1 << 4)  /* poll yielded this channel */
#define EFX_CHANNEL_OWNED \
	(EFX_CHANNEL_STATE_NAPI | EFX_CHANNEL_STATE_POLL)
#define EFX_CHANNEL_LOCKED \
	(EFX_CHANNEL_OWNED | EFX_CHANNEL_STATE_DISABLED)
#define EFX_CHANNEL_USER_PEND \
	(EFX_CHANNEL_STATE_POLL | EFX_CHANNEL_STATE_POLL_YIELD)
#endif /* CONFIG_NET_RX_BUSY_POLL */
	struct efx_special_buffer eventq;
	unsigned int eventq_mask;
	unsigned int eventq_read_ptr;
	int event_test_cpu;

	unsigned int irq_count;
	unsigned int irq_mod_score;
#ifdef CONFIG_RFS_ACCEL
	unsigned int rfs_filters_added;
#endif

#ifdef CONFIG_SFC_DEBUGFS
	struct dentry *debug_dir;
#endif
#if defined(EFX_USE_KCOMPAT) && defined(EFX_NEED_SAVE_MSIX_MESSAGES)
	struct msi_msg msix_msg;
#endif
#if defined(EFX_USE_KCOMPAT) && defined(EFX_NEED_UNMASK_MSIX_VECTORS)
	u32 msix_ctrl;
#endif

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_SFC_LRO)
	struct efx_ssr_state ssr;
#endif

	unsigned n_rx_tobe_disc;
	unsigned n_rx_ip_hdr_chksum_err;
	unsigned n_rx_tcp_udp_chksum_err;
	unsigned n_rx_eth_crc_err;
	unsigned n_rx_mcast_mismatch;
	unsigned n_rx_frm_trunc;
	unsigned n_rx_overlength;
	unsigned n_skbuff_leaks;
	unsigned int n_rx_nodesc_trunc;
	unsigned int n_rx_merge_events;
	unsigned int n_rx_merge_packets;

	unsigned int rx_pkt_n_frags;
	unsigned int rx_pkt_index;

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_WITH_VMWARE_NETQ)
	/** Used to track use by VMWare netqueue code. */
	unsigned netq_flags;
	struct net_device_stats	netq_stats;
	char irqid[IFNAMSIZ + 8];
#endif

	struct efx_rx_queue rx_queue;
	struct efx_tx_queue tx_queue[2];

#ifdef CONFIG_SFC_PTP
	enum efx_sync_events_state sync_events_state;
	u32 sync_timestamp_major;
	u32 sync_timestamp_minor;
#endif
	uint64_t last_irq_tstamp;
	uint64_t last_napi_entry_tstamp;
	uint64_t last_napi_exit_tstamp;
	uint64_t last_channel_process_complete_tstamp;
	uint64_t handle_rx1_tstamp;
	uint64_t handle_tx1_tstamp;
	uint64_t handle_mcdi1_tstamp;
	uint64_t handle_driver1_tstamp;
	uint64_t handle_drvgen1_tstamp;
	uint64_t handle_rx2_tstamp;
	uint64_t handle_tx2_tstamp;
	uint64_t handle_mcdi2_tstamp;
	uint64_t handle_driver2_tstamp;
	uint64_t handle_drvgen2_tstamp;
};

#ifdef CONFIG_NET_RX_BUSY_POLL
static inline void efx_channel_init_lock(struct efx_channel *channel)
{
	spin_lock_init(&channel->state_lock);
}

/* Called from the device poll routine to get ownership of a channel. */
static inline bool efx_channel_lock_napi(struct efx_channel *channel)
{
	bool rc = true;

	spin_lock_bh(&channel->state_lock);
	if (channel->state & EFX_CHANNEL_LOCKED) {
		WARN_ON(channel->state & EFX_CHANNEL_STATE_NAPI);
		channel->state |= EFX_CHANNEL_STATE_NAPI_YIELD;
		rc = false;
	} else {
		/* we don't care if someone yielded */
		channel->state = EFX_CHANNEL_STATE_NAPI;
	}
	spin_unlock_bh(&channel->state_lock);
	return rc;
}

static inline void efx_channel_unlock_napi(struct efx_channel *channel)
{
	spin_lock_bh(&channel->state_lock);
	WARN_ON(channel->state &
		(EFX_CHANNEL_STATE_POLL | EFX_CHANNEL_STATE_NAPI_YIELD));

	channel->state &= EFX_CHANNEL_STATE_DISABLED;
	spin_unlock_bh(&channel->state_lock);
}

/* Called from efx_busy_poll(). */
static inline bool efx_channel_lock_poll(struct efx_channel *channel)
{
	bool rc = true;

	spin_lock_bh(&channel->state_lock);
	if ((channel->state & EFX_CHANNEL_LOCKED)) {
		channel->state |= EFX_CHANNEL_STATE_POLL_YIELD;
		rc = false;
	} else {
		/* preserve yield marks */
		channel->state |= EFX_CHANNEL_STATE_POLL;
	}
	spin_unlock_bh(&channel->state_lock);
	return rc;
}

/* Returns true if NAPI tried to get the channel while it was locked. */
static inline void efx_channel_unlock_poll(struct efx_channel *channel)
{
	spin_lock_bh(&channel->state_lock);
	WARN_ON(channel->state & EFX_CHANNEL_STATE_NAPI);

	/* will reset state to idle, unless channel is disabled */
	channel->state &= EFX_CHANNEL_STATE_DISABLED;
	spin_unlock_bh(&channel->state_lock);
}

/* True if a socket is polling, even if it did not get the lock. */
static inline bool efx_channel_busy_polling(struct efx_channel *channel)
{
	WARN_ON(!(channel->state & EFX_CHANNEL_OWNED));
	return channel->state & EFX_CHANNEL_USER_PEND;
}

static inline void efx_channel_enable(struct efx_channel *channel)
{
	spin_lock_bh(&channel->state_lock);
	channel->state = EFX_CHANNEL_STATE_IDLE;
	spin_unlock_bh(&channel->state_lock);
}

/* False if the channel is currently owned. */
static inline bool efx_channel_disable(struct efx_channel *channel)
{
	bool rc = true;

	spin_lock_bh(&channel->state_lock);
	if (channel->state & EFX_CHANNEL_OWNED)
		rc = false;
	channel->state |= EFX_CHANNEL_STATE_DISABLED;
	spin_unlock_bh(&channel->state_lock);

	return rc;
}

#else /* CONFIG_NET_RX_BUSY_POLL */

static inline void efx_channel_init_lock(struct efx_channel *channel)
{
}

static inline bool efx_channel_lock_napi(struct efx_channel *channel)
{
	return true;
}

static inline void efx_channel_unlock_napi(struct efx_channel *channel)
{
}

static inline bool efx_channel_lock_poll(struct efx_channel *channel)
{
	return false;
}

static inline void efx_channel_unlock_poll(struct efx_channel *channel)
{
}

static inline bool efx_channel_busy_polling(struct efx_channel *channel)
{
	return false;
}

static inline void efx_channel_enable(struct efx_channel *channel)
{
}

static inline bool efx_channel_disable(struct efx_channel *channel)
{
	return true;
}
#endif /* CONFIG_NET_RX_BUSY_POLL */

/**
 * struct efx_msi_context - Context for each MSI
 * @efx: The associated NIC
 * @index: Index of the channel/IRQ
 * @name: Name of the channel/IRQ
 *
 * Unlike &struct efx_channel, this is never reallocated and is always
 * safe for the IRQ handler to access.
 */
struct efx_msi_context {
	struct efx_nic *efx;
	unsigned int index;
	char name[IFNAMSIZ + 6];
};

/**
 * struct efx_channel_type - distinguishes traffic and extra channels
 * @handle_no_channel: Handle failure to allocate an extra channel
 * @pre_probe: Set up extra state prior to initialisation
 * @post_remove: Tear down extra state after finalisation, if allocated.
 *	May be called on channels that have not been probed.
 * @get_name: Generate the channel's name (used for its IRQ handler)
 * @copy: Copy the channel state prior to reallocation.  May be %NULL if
 *	reallocation is not supported.
 * @receive_skb: Handle an skb ready to be passed to netif_receive_skb()
 * @keep_eventq: Flag for whether event queue should be kept initialised
 *	while the device is stopped
 */
struct efx_channel_type {
	void (*handle_no_channel)(struct efx_nic *);
	int (*pre_probe)(struct efx_channel *);
	void (*post_remove)(struct efx_channel *);
	void (*get_name)(struct efx_channel *, char *buf, size_t len);
	struct efx_channel *(*copy)(const struct efx_channel *);
#if defined(EFX_NOT_UPSTREAM)
	bool (*receive_skb)(struct efx_channel *, struct sk_buff *,
			    bool vlan_tagged, u16 vlan_tci);
#else
	bool (*receive_skb)(struct efx_channel *, struct sk_buff *);
#endif
	bool keep_eventq;
};

enum efx_led_mode {
	EFX_LED_OFF	= 0,
	EFX_LED_ON	= 1,
	EFX_LED_DEFAULT	= 2
};

#define STRING_TABLE_LOOKUP(val, member) \
	((val) < member ## _max) ? member ## _names[val] : "(invalid)"

extern const char *const efx_loopback_mode_names[];
extern const unsigned int efx_loopback_mode_max;
#define LOOPBACK_MODE(efx) \
	STRING_TABLE_LOOKUP((efx)->loopback_mode, efx_loopback_mode)

extern const char *const efx_interrupt_mode_names[];
extern const unsigned int efx_interrupt_mode_max;
#define INT_MODE(efx) \
	STRING_TABLE_LOOKUP(efx->interrupt_mode, efx_interrupt_mode)

extern const char *const efx_reset_type_names[];
extern const unsigned int efx_reset_type_max;
#define RESET_TYPE(type) \
	STRING_TABLE_LOOKUP(type, efx_reset_type)

enum efx_int_mode {
	/* Be careful if altering to correct macro below */
	EFX_INT_MODE_MSIX = 0,
	EFX_INT_MODE_MSI = 1,
	EFX_INT_MODE_LEGACY = 2,
	EFX_INT_MODE_MAX	/* Insert any new items before this */
};
#define EFX_INT_MODE_USE_MSI(x) (((x)->interrupt_mode) <= EFX_INT_MODE_MSI)

enum nic_state {
	STATE_UNINIT = 0,	/* device being probed/removed or is frozen */
	STATE_READY = 1,	/* hardware ready and netdev registered */
	STATE_DISABLED = 2,	/* device disabled due to hardware errors */
	STATE_RECOVERY = 3,	/* device recovering from PCI error */
};

/* Forward declaration */
struct efx_nic;

/* Pseudo bit-mask flow control field */
#define EFX_FC_RX	FLOW_CTRL_RX
#define EFX_FC_TX	FLOW_CTRL_TX
#define EFX_FC_AUTO	4

/**
 * struct efx_link_state - Current state of the link
 * @up: Link is up
 * @fd: Link is full-duplex
 * @fc: Actual flow control flags
 * @speed: Link speed (Mbps)
 */
struct efx_link_state {
	bool up;
	bool fd;
	u8 fc;
	unsigned int speed;
};

static inline bool efx_link_state_equal(const struct efx_link_state *left,
					const struct efx_link_state *right)
{
	return left->up == right->up && left->fd == right->fd &&
		left->fc == right->fc && left->speed == right->speed;
}

/**
 * struct efx_phy_operations - Efx PHY operations table
 * @probe: Probe PHY and initialise efx->mdio.mode_support, efx->mdio.mmds,
 *	efx->loopback_modes.
 * @init: Initialise PHY
 * @fini: Shut down PHY
 * @reconfigure: Reconfigure PHY (e.g. for new link parameters)
 * @poll: Update @link_state and report whether it changed.
 *	Serialised by the mac_lock.
 * @get_settings: Get ethtool settings. Serialised by the mac_lock.
 * @set_settings: Set ethtool settings. Serialised by the mac_lock.
 * @set_npage_adv: Set abilities advertised in (Extended) Next Page
 *	(only needed where AN bit is set in mmds)
 * @test_alive: Test that PHY is 'alive' (online)
 * @test_name: Get the name of a PHY-specific test/result
 * @run_tests: Run tests and record results as appropriate (offline).
 *	Flags are the ethtool tests flags.
 */
struct efx_phy_operations {
	int (*probe) (struct efx_nic *efx);
	int (*init) (struct efx_nic *efx);
	void (*fini) (struct efx_nic *efx);
	void (*remove) (struct efx_nic *efx);
	int (*reconfigure) (struct efx_nic *efx);
	bool (*poll) (struct efx_nic *efx);
	void (*get_settings) (struct efx_nic *efx,
			      struct ethtool_cmd *ecmd);
	int (*set_settings) (struct efx_nic *efx,
			     struct ethtool_cmd *ecmd);
	void (*set_npage_adv) (struct efx_nic *efx, u32);
	int (*test_alive) (struct efx_nic *efx);
	const char *(*test_name) (struct efx_nic *efx, unsigned int index);
	int (*run_tests) (struct efx_nic *efx, int *results, unsigned flags);
	int (*get_module_eeprom) (struct efx_nic *efx,
				  struct ethtool_eeprom *ee,
				  u8 *data);
	int (*get_module_info) (struct efx_nic *efx,
				struct ethtool_modinfo *modinfo);
};

/**
 * enum efx_phy_mode - PHY operating mode flags
 * @PHY_MODE_NORMAL: on and should pass traffic
 * @PHY_MODE_TX_DISABLED: on with TX disabled
 * @PHY_MODE_LOW_POWER: set to low power through MDIO
 * @PHY_MODE_OFF: switched off through external control
 * @PHY_MODE_SPECIAL: on but will not pass traffic
 */
enum efx_phy_mode {
	PHY_MODE_NORMAL		= 0,
	PHY_MODE_TX_DISABLED	= 1,
	PHY_MODE_LOW_POWER	= 2,
	PHY_MODE_OFF		= 4,
	PHY_MODE_SPECIAL	= 8,
};

static inline bool efx_phy_mode_disabled(enum efx_phy_mode mode)
{
	return !!(mode & ~PHY_MODE_TX_DISABLED);
}

/**
 * struct efx_hw_stat_desc - Description of a hardware statistic
 * @name: Name of the statistic as visible through ethtool, or %NULL if
 *	it should not be exposed
 * @dma_width: Width in bits (0 for non-DMA statistics)
 * @offset: Offset within stats (ignored for non-DMA statistics)
 */
struct efx_hw_stat_desc {
	const char *name;
	u16 dma_width;
	u16 offset;
};

/* Number of bits used in a multicast filter hash address */
#define EFX_MCAST_HASH_BITS 8

/* Number of (single-bit) entries in a multicast filter hash */
#define EFX_MCAST_HASH_ENTRIES (1 << EFX_MCAST_HASH_BITS)

/* An Efx multicast filter hash */
union efx_multicast_hash {
	u8 byte[EFX_MCAST_HASH_ENTRIES / 8];
	efx_oword_t oword[EFX_MCAST_HASH_ENTRIES / sizeof(efx_oword_t) / 8];
};

/* Efx Error condition statistics */
struct efx_nic_errors {
	atomic_t missing_event;
	atomic_t rx_reset;
	atomic_t rx_desc_fetch;
	atomic_t tx_desc_fetch;
	atomic_t spurious_tx;

#ifdef CONFIG_SFC_DEBUGFS
	struct dentry *debug_dir;
#endif
};

struct vfdi_status;

/**
 * struct efx_nic - an Efx NIC
 * @name: Device name (net device name or bus id before net device registered)
 * @pci_dev: The PCI device
 * @node: List node for maintaning primary/secondary function lists
 * @primary: &struct efx_nic instance for the primary function of this
 *	controller.  May be the same structure, and may be %NULL if no
 *	primary function is bound.  Serialised by rtnl_lock.
 * @secondary_list: List of &struct efx_nic instances for the secondary PCI
 *	functions of the controller, if this is for the primary function.
 *	Serialised by rtnl_lock.
 * @revision: Hardware architecture revision
 * @dl_revision: Revision name for driverlink
 * @type: Controller type attributes
 * @legacy_irq: IRQ number
 * @reset_work: Scheduled reset workitem
 * @membase_phys: Memory BAR value as physical address
 * @membase: Memory BAR value
 * @interrupt_mode: Interrupt mode
 * @timer_quantum_ns: Interrupt timer quantum, in nanoseconds
 * @irq_rx_adaptive: Adaptive IRQ moderation enabled for RX event queues
 * @irq_rx_moderation: IRQ moderation time for RX event queues
 * @msg_enable: Log message enable flags
 * @state: Device state number (%STATE_*). Serialised by the rtnl_lock.
 * @reset_pending: Bitmask for pending resets
 * @tx_queue: TX DMA queues
 * @rx_queue: RX DMA queues
 * @channel: Channels
 * @msi_context: Context for each MSI
 * @extra_channel_types: Types of extra (non-traffic) channels that
 *	should be allocated for this NIC
 * @rxq_entries: Size of receive queues requested by user.
 * @txq_entries: Size of transmit queues requested by user.
 * @txq_stop_thresh: TX queue fill level at or above which we stop it.
 * @txq_wake_thresh: TX queue fill level at or below which we wake it.
 * @tx_dc_entries: Number of entries in each TX queue descriptor cache
 * @rx_dc_entries: Number of entries in each RX queue descriptor cache
 * @tx_dc_base: Base qword address in SRAM of TX queue descriptor caches
 * @rx_dc_base: Base qword address in SRAM of RX queue descriptor caches
 * @sram_lim_qw: Qword address limit of SRAM
 * @next_buffer_table: Next buffer table index to use
 * @farch_resources: Falcon driverlink parameters
 * @ef10_resources: EF10 driverlink parameters
 * @n_channels: Number of channels in use
 * @n_rx_channels: Number of channels used for RX (= number of RX queues)
 * @n_rss_channels: Number of rx channels available for RSS.
 * @rss_spread: Number of event queues to spread traffic over.
 * @n_tx_channels: Number of channels used for TX
 * @tx_channel_offset: Offset of zeroth channel used for TX.
 * @tx_channel_stride: Stride between channels used for TX (only in VMware port)
 * @n_wanted_channels: Number of interrupts efx_probe_interrupts() attempted
 *     to enable.
 * @n_rx_netqs: Number of receive NETQs including default (only in VMware port)
 * @n_rx_netqs_no_rss: Number of receive NETQs without RSS (only in VMware port)
 * @rx_ip_align: RX DMA address offset to have IP header aligned in
 *	in accordance with NET_IP_ALIGN
 * @rx_dma_len: Current maximum RX DMA length
 * @rx_buffer_order: Order (log2) of number of pages for each RX buffer
 * @rx_buffer_truesize: Amortised allocation size of an RX buffer,
 *	for use in sk_buff::truesize
 * @rx_prefix_size: Size of RX prefix before packet data
 * @rx_packet_hash_offset: Offset of RX flow hash from start of packet data
 *	(valid only if @rx_prefix_size != 0; always negative)
 * @rx_packet_len_offset: Offset of RX packet length from start of packet data
 *	(valid only for NICs that set %EFX_RX_PKT_PREFIX_LEN; always negative)
 * @rx_packet_ts_offset: Offset of timestamp from start of packet data
 *	(valid only if channel->sync_timestamps_enabled; always negative)
 * @rx_hash_key: Toeplitz hash key for RSS
 * @rx_indir_table: Indirection table for RSS
 * @rx_scatter: Scatter mode enabled for receives
 * @cpu_channel_map: Mapping of CPUs to channels. Used to assign flows.
 * @errors: Error condition stats
 * @int_error_count: Number of internal errors seen recently
 * @int_error_expire: Time at which error count will be expired
 * @irq_soft_enabled: Are IRQs soft-enabled? If not, IRQ handler will
 *	acknowledge but do nothing else.
 * @irq_status: Interrupt status buffer
 * @irq_zero_count: Number of legacy IRQs seen with queue flags == 0
 * @irq_level: IRQ level/index for IRQs not triggered by an event queue
 * @selftest_work: Work item for asynchronous self-test
 * @mtd_list: List of MTDs attached to the NIC
 * @nic_data: Hardware dependent state
 * @mcdi: Management-Controller-to-Driver Interface state
 * @mac_lock: MAC access lock. Protects @port_enabled, @link_up, @phy_mode,
 *	efx_monitor() and efx_mac_work()
 * @mac_work: Work item for changing MAC promiscuity and multicast hash
 * @port_enabled: Port enabled indicator.
 *	Serialises efx_stop_all(), efx_start_all(), efx_monitor() and
 *	efx_mac_work() with kernel interfaces. Safe to read under any
 *	one of the rtnl_lock, mac_lock, or netif_tx_lock, but all three must
 *	be held to modify it.
 * @port_initialized: Port initialized?
 * @net_dev: Operating system network device. Consider holding the rtnl lock
 * @stats_buffer: DMA buffer for statistics
 * @phy_type: PHY type
 * @phy_op: PHY interface
 * @phy_data: PHY private data (including PHY-specific stats)
 * @mdio: PHY MDIO interface
 * @mdio_bus: PHY MDIO bus ID (only used by Siena)
 * @phy_mode: PHY operating mode. Serialised by @mac_lock.
 * @link_advertising: Autonegotiation advertising flags
 * @link_state: Current state of the link
 * @n_link_state_changes: Number of times the link has changed state
 * @unicast_filter: Flag for Falcon-arch simple unicast filter.
 *	Protected by @mac_lock.
 * @multicast_hash: Multicast hash table for Falcon-arch.
 *	Protected by @mac_lock.
 * @wanted_fc: Wanted flow control flags
 * @fc_disable: When non-zero flow control is disabled. Typically used to
 *	ensure that network back pressure doesn't delay dma queue flushes.
 *	Serialised by the rtnl lock.
 * @loopback_mode: Loopback status
 * @loopback_modes: Supported loopback mode bitmask
 * @loopback_selftest: Offline self-test private state
 * @filter_sem: Filter table rw_semaphore, for freeing the table
 * @filter_lock: Filter table lock, for mere content changes
 * @filter_state: Architecture-dependent filter table state
 * @rps_flow_id: Flow IDs of filters allocated for accelerated RFS,
 *	indexed by filter ID
 * @rps_expire_index: Next index to check for expiry in @rps_flow_id
 * @dl_info: Linked list of hardware parameters exposed through driverlink
 * @dl_node: Driverlink port list
 * @dl_device_list: Driverlink device list
 * @active_queues: Count of RX and TX queues that haven't been flushed and drained.
 * @rxq_flush_pending: Count of number of receive queues that need to be flushed.
 *	Decremented when the efx_flush_rx_queue() is called.
 * @rxq_flush_outstanding: Count of number of RX flushes started but not yet
 *	completed (either success or failure). Not used when MCDI is used to
 *	flush receive queues.
 * @flush_wq: wait queue used by efx_nic_flush_queues() to wait for flush completions.
 * @max_vfs: Number of VFs to be initialised by the driver.
 * @vf_count: Number of VFs intended to be enabled.
 * @vf_init_count: Number of VFs that have been fully initialised.
 * @vi_scale: log2 number of vnics per VF.
 * @ptp_data: PTP state data
 * @monitor_work: Hardware monitor workitem
 * @biu_lock: BIU (bus interface unit) lock
 * @last_irq_cpu: Last CPU to handle a possible test interrupt.  This
 *	field is used by efx_test_interrupts() to verify that an
 *	interrupt has occurred.
 * @stats_lock: Statistics update lock. Must be held when calling
 *	efx_nic_type::{update,start,stop}_stats.
 * @n_rx_noskb_drops: Count of RX packets dropped due to failure to allocate an skb
 * @forward_fcs: Flag to enable appending of 4-byte Frame Checksum to Rx packet
 * @mc_promisc: Whether in multicast promiscuous mode when last changed
 * @proxy_admin: State for proxy auth admin
 * @proxy_admin_lock: RW lock for proxy auth admin locking
 * @proxy_admin_mutex: Mutex for serialising proxy auth admin shutdown
 * @proxy_admin_stop_work: Work item for stopping proxy auth from atomic context.
 * This is stored in the private area of the &struct net_device.
 */
struct efx_nic {
	/* The following fields should be written very rarely */

	char name[IFNAMSIZ];
	struct list_head node;
	struct efx_nic *primary;
	struct list_head secondary_list;
	struct pci_dev *pci_dev;
	unsigned int port_num;
	const struct efx_nic_type *type;
	int legacy_irq;
	bool eeh_disabled_legacy_irq;
	struct work_struct reset_work;
	resource_size_t membase_phys;
	void __iomem *membase;

	enum efx_int_mode interrupt_mode;
	unsigned int timer_quantum_ns;
	bool irq_rx_adaptive;
	unsigned int irq_rx_moderation;
	enum efx_rss_mode rss_mode;
	u32 msg_enable;

	enum nic_state state;
	unsigned long reset_pending;

	struct efx_channel *channel[EFX_MAX_CHANNELS];
	struct efx_msi_context msi_context[EFX_MAX_CHANNELS];
	const struct efx_channel_type *
	extra_channel_type[EFX_MAX_EXTRA_CHANNELS];

	unsigned rxq_entries;
	unsigned txq_entries;
	unsigned int txq_stop_thresh;
	unsigned int txq_wake_thresh;

	unsigned tx_dc_entries;
	unsigned rx_dc_entries;
	unsigned tx_dc_base;
	unsigned rx_dc_base;
	unsigned sram_lim_qw;
	unsigned next_buffer_table;
	struct efx_dl_falcon_resources farch_resources;
	struct efx_dl_ef10_resources ef10_resources;
	struct efx_dl_aoe_resources aoe_resources;

	unsigned int max_channels;
	unsigned int max_tx_channels;
	unsigned n_channels;
	unsigned n_rx_channels;
	unsigned n_rss_channels;
	unsigned rss_spread;
	unsigned n_tx_channels;
	unsigned tx_channel_offset;
	unsigned n_wanted_channels;
#if defined(EFX_NOT_UPSTREAM) && defined(EFX_WITH_VMWARE_NETQ)
	unsigned int n_rx_netqs;
	unsigned int n_rx_netqs_no_rss;
#endif
	unsigned int rx_ip_align;
	unsigned int rx_dma_len;
	unsigned int rx_buffer_order;
	unsigned int rx_buffer_truesize;
	unsigned int rx_page_buf_step;
	unsigned int rx_bufs_per_page;
	unsigned int rx_pages_per_batch;
	unsigned int rx_prefix_size;
	int rx_packet_hash_offset;
	int rx_packet_len_offset;
	int rx_packet_ts_offset;
	u8 rx_hash_key[40];
	u32 rx_indir_table[128];
	bool rx_scatter;

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_ENABLE_SFC_XPS)
	int *cpu_channel_map;
#endif

#if defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_SARFS)
	struct efx_sarfs_state sarfs_state;
#endif

	struct efx_nic_errors errors;
	unsigned int_error_count;
	unsigned long int_error_expire;

	bool irq_soft_enabled;
	struct efx_buffer irq_status;
	unsigned irq_zero_count;
	unsigned long irq_level;
	struct delayed_work selftest_work;

#ifdef CONFIG_SFC_MTD
	struct list_head mtd_list;
#endif

#if defined(EFX_USE_KCOMPAT) && defined(EFX_NEED_PCI_VPD_ATTR)
	struct pci_vpd_pci22 *vpd;
#endif

	void *nic_data;
	struct efx_mcdi_data *mcdi;

	struct mutex mac_lock;
	struct work_struct mac_work;
	bool port_enabled;

	bool mc_bist_for_other_fn;
	bool port_initialized;
	struct net_device *net_dev;
#if defined(EFX_USE_KCOMPAT) && !defined(EFX_HAVE_NDO_SET_FEATURES)
	bool rx_checksum_enabled;
#endif
#if defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_SFC_LRO)
	bool lro_available;
#ifndef NETIF_F_LRO
	bool lro_enabled;
#endif
#endif

	struct efx_buffer stats_buffer;
	u64 rx_nodesc_drops_total;
	u64 rx_nodesc_drops_while_down;
	bool rx_nodesc_drops_prev_state;

#if defined(EFX_USE_KCOMPAT) && !defined(EFX_USE_NETDEV_PERM_ADDR)
	unsigned char perm_addr[ETH_ALEN] __aligned(2);
#endif
#if defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_FAKE_VLAN_RX_ACCEL)
	struct vlan_group *vlan_group;
#endif

	unsigned int phy_type;
	char phy_name[20];
	const struct efx_phy_operations *phy_op;
	void *phy_data;
	struct mdio_if_info mdio;
	unsigned int mdio_bus;
	enum efx_phy_mode phy_mode;

	u32 link_advertising;
	struct efx_link_state link_state;
	unsigned int n_link_state_changes;

	bool unicast_filter;
	union efx_multicast_hash multicast_hash;
	u8 wanted_fc;
	unsigned fc_disable;

	enum efx_loopback_mode loopback_mode;
	u64 loopback_modes;
	void *loopback_selftest;

	struct rw_semaphore filter_sem;
	spinlock_t filter_lock;
	void *filter_state;
#ifdef CONFIG_RFS_ACCEL
#define RPS_FLOW_ID_INVALID 0xFFFFFFFF
	u32 *rps_flow_id;
	unsigned int rps_expire_index;
#endif

	struct efx_dl_device_info *dl_info;
	struct list_head dl_node;
	struct list_head dl_device_list;
#ifdef EFX_NOT_UPSTREAM
/* Mutex protecting @dl_block_kernel_count and corresponding per-client state */
	struct mutex dl_block_kernel_mutex;
/* Number of times Driverlink clients are blocking the kernel stack from
 * receiving packets
 */
	unsigned int dl_block_kernel_count[EFX_DL_FILTER_BLOCK_KERNEL_MAX];
#endif

#ifdef CONFIG_SFC_DEBUGFS
/* NIC debugfs directory */
	struct dentry *debug_dir;
/* NIC debugfs sym-link (nic_eth%d) */
	struct dentry *debug_symlink;
/* Port debugfs directory */
	struct dentry *debug_port_dir;
/* Port debugfs sym-link (if_eth%d) */
	struct dentry *debug_port_symlink;
#endif
#if defined(EFX_NOT_UPSTREAM) && defined(EFX_WITH_VMWARE_NETQ)
	int netq_active;
#endif

	atomic_t active_queues;
	atomic_t rxq_flush_pending;
	atomic_t rxq_flush_outstanding;
	wait_queue_head_t flush_wq;

#ifdef CONFIG_SFC_SRIOV
	int max_vfs;
	unsigned vf_count;
	unsigned vf_buftbl_base;
	unsigned vf_init_count;
	unsigned vi_scale;
#endif

#ifdef CONFIG_SFC_PTP
	struct efx_ptp_data *ptp_data;
#endif

#if defined(EFX_NOT_UPSTREAM) && defined(CONFIG_SFC_AOE)
	struct efx_aoe_data *aoe_data;
#endif

	/* The following fields may be written more often */

	struct delayed_work monitor_work ____cacheline_aligned_in_smp;
	spinlock_t biu_lock;
	int last_irq_cpu;
#if defined(EFX_USE_KCOMPAT) && !defined(EFX_USE_NETDEV_STATS)
	struct net_device_stats stats;
#endif
	spinlock_t stats_lock;
	atomic_t n_rx_noskb_drops;
	char *vpd_sn;
	bool forward_fcs;
	bool mc_promisc;
#ifdef EFX_USE_MCDI_PROXY_AUTH
	struct proxy_admin_state *proxy_admin_state;
	rwlock_t proxy_admin_lock;
	struct mutex proxy_admin_mutex;
	struct work_struct proxy_admin_stop_work;
#endif
};

static inline int efx_dev_registered(struct efx_nic *efx)
{
	return efx->net_dev->reg_state == NETREG_REGISTERED;
}

static inline unsigned int efx_port_num(struct efx_nic *efx)
{
	return efx->port_num;
}

#ifdef CONFIG_SFC_MTD
struct efx_mtd_partition {
	struct list_head node;
	struct mtd_info mtd;
	const char *dev_type_name;
	const char *type_name;
	char name[IFNAMSIZ + 20];
};
#endif

/**
 * struct efx_nic_type - Efx device type definition
 * @is_vf: Tells whether the function is a VF or PF
 * @mem_bar: Get the memory BAR
 * @mem_map_size: Get memory BAR mapped size
 * @probe: Probe the controller
 * @remove: Free resources allocated by probe()
 * @init: Initialise the controller
 * @dimension_resources: Dimension controller resources (buffer table,
 *	and VIs once the available interrupt resources are clear)
 * @fini: Shut down the controller
 * @monitor: Periodic function for polling link state and hardware monitor
 * @map_reset_reason: Map ethtool reset reason to a reset method
 * @map_reset_flags: Map ethtool reset flags to a reset method, if possible
 * @reset: Reset the controller hardware and possibly the PHY.  This will
 *	be called while the controller is uninitialised.
 * @probe_port: Probe the MAC and PHY
 * @remove_port: Free resources allocated by probe_port()
 * @handle_global_event: Handle a "global" event (may be %NULL)
 * @fini_dmaq: Flush and finalise DMA queues (RX and TX queues)
 * @prepare_flush: Prepare the hardware for flushing the DMA queues
 *	(for Falcon architecture)
 * @finish_flush: Clean up after flushing the DMA queues (for Falcon
 *	architecture)
 * @prepare_flr: Prepare for an FLR
 * @finish_flr: Clean up after an FLR
 * @describe_stats: Describe statistics for ethtool
 * @update_stats: Update statistics not provided by event handling.
 *	Either argument may be %NULL.
 * @start_stats: Start the regular fetching of statistics
 * @pull_stats: Pull stats from the NIC and wait until they arrive.
 * @stop_stats: Stop the regular fetching of statistics
 * @set_id_led: Set state of identifying LED or revert to automatic function
 * @push_irq_moderation: Apply interrupt moderation value
 * @reconfigure_port: Push loopback/power/txdis changes to the MAC and PHY
 * @prepare_enable_fc_tx: Prepare MAC to enable pause frame TX (may be %NULL)
 * @reconfigure_mac: Push MAC address, MTU, flow control and filter settings
 *	to the hardware.  Serialised by the mac_lock.
 * @check_mac_fault: Check MAC fault state. True if fault present.
 * @get_wol: Get WoL configuration from driver state
 * @set_wol: Push WoL configuration to the NIC
 * @resume_wol: Synchronise WoL state between driver and MC (e.g. after resume)
 * @test_chip: Test registers and memory.  Should use efx_test_memory()
 *	and may use efx_farch_test_registers(), and is expected to reset
 *	the NIC.
 * @test_memory: Test read/write functionality of memory blocks, using
 *	the given test pattern generator
 * @test_nvram: Test validity of NVRAM contents
 * @mcdi_request: Send an MCDI request with the given header and SDU.
 *	The SDU length may be any value from 0 up to the protocol-
 *	defined maximum, but its buffer will be padded to a multiple
 *	of 4 bytes.
 * @mcdi_poll_response: Test whether an MCDI response is available.
 * @mcdi_read_response: Read the MCDI response PDU.  The offset will
 *	be a multiple of 4.  The length may not be, but the buffer
 *	will be padded so it is safe to round up.
 * @mcdi_poll_reboot: Test whether the MCDI has rebooted.  If so,
 *	return an appropriate error code for aborting any current
 *	request; otherwise return 0.
 * @irq_enable_master: Enable IRQs on the NIC.  Each event queue must
 *	be separately enabled after this.
 * @irq_test_generate: Generate a test IRQ
 * @irq_disable_non_ev: Disable non-event IRQs on the NIC.  Each event
 *	queue must be separately disabled before this.
 * @irq_handle_msi: Handle MSI for a channel.  The @dev_id argument is
 *	a pointer to the &struct efx_msi_context for the channel.
 * @irq_handle_legacy: Handle legacy interrupt.  The @dev_id argument
 *	is a pointer to the &struct efx_nic.
 * @tx_probe: Allocate resources for TX queue
 * @tx_init: Initialise TX queue on the NIC
 * @tx_remove: Free resources for TX queue
 * @tx_write: Write TX descriptors and doorbell
 * @rx_push_rss_config: Write RSS hash key and indirection table to the NIC
 * @rx_probe: Allocate resources for RX queue
 * @rx_init: Initialise RX queue on the NIC
 * @rx_remove: Free resources for RX queue
 * @rx_write: Write RX descriptors and doorbell
 * @rx_defer_refill: Generate a refill reminder event
 * @ev_probe: Allocate resources for event queue
 * @ev_init: Initialise event queue on the NIC
 * @ev_fini: Deinitialise event queue on the NIC
 * @ev_remove: Free resources for event queue
 * @ev_process: Process events for a queue, up to the given NAPI quota
 * @ev_read_ack: Acknowledge read events on a queue, rearming its IRQ
 * @ev_test_generate: Generate a test event
 * @filter_table_probe: Probe filter capabilities and set up filter software state
 * @filter_table_restore: Restore filters removed from hardware
 * @filter_table_remove: Remove filters from hardware and tear down software state
 * @filter_update_rx_scatter: Update filters after change to rx scatter setting
 * @filter_insert: add or replace a filter
 * @filter_remove_safe: remove a filter by ID, carefully
#ifdef __VMKLNX__
 * @filter_get_unsafe_id: get filter ID which uniquely identify it but
 *	possibly has no extra information for safety checks
 * @filter_remove_unsafe: remove a filter by ID
#endif
 * @filter_get_safe: retrieve a filter by ID, carefully
 * @filter_clear_rx: Remove all RX filters whose priority is less than or
 *	equal to the given priority and is not %EFX_FILTER_PRI_AUTO
 * @filter_redirect: update the queue for an existing RX filter
 * @filter_count_rx_used: Get the number of filters in use at a given priority
 * @filter_get_rx_id_limit: Get maximum value of a filter id, plus 1
 * @filter_get_rx_ids: Get list of RX filters at a given priority
 * @filter_rfs_insert: Add or replace a filter for RFS.  This must be
 *	atomic.  The hardware change may be asynchronous but should
 *	not be delayed for long.  It may fail if this can't be done
 *	atomically.
 * @filter_rfs_expire_one: Consider expiring a filter inserted for RFS.
 *	This must check whether the specified table entry is used by RFS
 *	and that rps_may_expire_flow() returns true for it.
 * @vport_filter_insert: add filter for another vport (for driverlink)
 * @vport_filter_remove: remove filter for another vport (for driverlink)
 * @mtd_probe: Probe and add MTD partitions associated with this net device,
 *	 using efx_mtd_add()
 * @mtd_rename: Set an MTD partition name using the net device name
 * @mtd_read: Read from an MTD partition
 * @mtd_erase: Erase part of an MTD partition
 * @mtd_write: Write to an MTD partition
 * @mtd_sync: Wait for write-back to complete on MTD partition.  This
 *	also notifies the driver that a writer has finished using this
 *	partition.
 * @ptp_write_host_time: Send host time to MC as part of sync protocol
 * @ptp_set_ts_sync_events: Enable or disable sync events for inline RX
 *	timestamping, possibly only temporarily for the purposes of a reset.
 * @ptp_set_ts_config: Set hardware timestamp configuration.  The flags
 *	and tx_type will already have been validated but this operation
 *	must validate and update rx_filter.
 * @sriov_init: Initialise VFs when vf-count is set via module parameter.
 * @sriov_fini: Disable sriov
 * @sriov_get_phys_port_id: Get the underlying physical port id.
 * @sriov_wanted: Check that max_vf > 0.
 * @sriov_respet: Reset sriov, siena only.
 * @sriov_configure: Enable VFs.
 * @sriov_flr: Handles FLR on a VF, siena only.
 * @sriov_set_vf_mac: Performs MCDI commands to delete and add back a new
 *       vport with new mac address.
 * @sriov_set_vf_vlan: Set up VF vlan.
 * @sriov_set_vf_spoofchk: Checks if sppokcheck is supported.
 * @sriov_get_vf_config: Gets VF config
 * @sriov_set_vf_link_state: Set VF Link state
 * @vswitching_probe: Allocate vswitches and vports.
 * @vswitching_restore: Restore vswitching following a reset.
 * @vswitching_remove: Free the vports and vswitches.
 * @get_mac_address: Get mac address from underlying vport/pport.
 * @set_mac_address: Set the MAC address of the device
 * @revision: Hardware architecture revision
 * @txd_ptr_tbl_base: TX descriptor ring base address
 * @rxd_ptr_tbl_base: RX descriptor ring base address
 * @buf_tbl_base: Buffer table base address
 * @evq_ptr_tbl_base: Event queue pointer table base address
 * @evq_rptr_tbl_base: Event queue read-pointer table base address
 * @max_dma_mask: Maximum possible DMA mask
 * @rx_prefix_size: Size of RX prefix before packet data
 * @rx_hash_offset: Offset of RX flow hash within prefix
 * @rx_ts_offset: Offset of timestamp within prefix
 * @rx_buffer_padding: Size of padding at end of RX packet
 * @can_rx_scatter: NIC is able to scatter packets to multiple buffers
 * @always_rx_scatter: NIC will always scatter packets to multiple buffers
 * @option_descriptors: NIC supports TX option descriptors
 * @min_interrupt_mode: Lowest capability interrupt mode supported
 *	from &enum efx_init_mode.
 * @max_interrupt_mode: Highest capability interrupt mode supported
 *	from &enum efx_init_mode.
 * @timer_period_max: Maximum period of interrupt timer (in ticks)
 * @farch_resources: Resources to be shared via driverlink (copied and
 *	updated as efx_nic::farch_resources)
 * @ef10_resources: Resources to be shared via driverlink (copied and
 *	updated as efx_nic::ef10_resources)
 * @offload_features: net_device feature flags for protocol offload
 *	features implemented in hardware
 * @mcdi_max_ver: Maximum MCDI version supported
 * @hwtstamp_filters: Mask of hardware timestamp filter types supported
 */
struct efx_nic_type {
	bool is_vf;
	unsigned int mem_bar;
	unsigned int (*mem_map_size)(struct efx_nic *efx);
	int (*probe)(struct efx_nic *efx);
	int (*dimension_resources)(struct efx_nic *efx);
	void (*remove)(struct efx_nic *efx);
	int (*init)(struct efx_nic *efx);
	void (*fini)(struct efx_nic *efx);
	void (*monitor)(struct efx_nic *efx);
	enum reset_type (*map_reset_reason)(enum reset_type reason);
	int (*map_reset_flags)(u32 *flags);
	int (*reset)(struct efx_nic *efx, enum reset_type method);
	int (*probe_port)(struct efx_nic *efx);
	void (*remove_port)(struct efx_nic *efx);
	bool (*handle_global_event)(struct efx_channel *channel, efx_qword_t *);
	int (*fini_dmaq)(struct efx_nic *efx);
	void (*prepare_flush)(struct efx_nic *efx);
	void (*finish_flush)(struct efx_nic *efx);
	void (*prepare_flr)(struct efx_nic *efx);
	void (*finish_flr)(struct efx_nic *efx);
	size_t (*describe_stats)(struct efx_nic *efx, u8 *names);
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_USE_NETDEV_STATS64)
	size_t (*update_stats)(struct efx_nic *efx, u64 *full_stats,
			       struct rtnl_link_stats64 *core_stats);
#else
	size_t (*update_stats)(struct efx_nic *efx, u64 *full_stats,
			       struct net_device_stats *core_stats);
#endif
	void (*start_stats)(struct efx_nic *efx);
	void (*pull_stats)(struct efx_nic *efx);
	void (*stop_stats)(struct efx_nic *efx);
	void (*set_id_led)(struct efx_nic *efx, enum efx_led_mode mode);
	void (*push_irq_moderation)(struct efx_channel *channel);
	int (*reconfigure_port)(struct efx_nic *efx);
	void (*prepare_enable_fc_tx)(struct efx_nic *efx);
	int (*reconfigure_mac)(struct efx_nic *efx);
	bool (*check_mac_fault)(struct efx_nic *efx);
	void (*get_wol)(struct efx_nic *efx, struct ethtool_wolinfo *wol);
	int (*set_wol)(struct efx_nic *efx, u32 type);
	void (*resume_wol)(struct efx_nic *efx);
	int (*test_chip)(struct efx_nic *efx, struct efx_self_tests *tests);
	int (*test_memory)(struct efx_nic *efx,
			   void (*pattern)(unsigned, efx_qword_t *, int, int),
			   int a, int b);
	int (*test_nvram)(struct efx_nic *efx);
	void (*mcdi_request)(struct efx_nic *efx,
			     const efx_dword_t *hdr, size_t hdr_len,
			     const efx_dword_t *sdu, size_t sdu_len);
	bool (*mcdi_poll_response)(struct efx_nic *efx);
	void (*mcdi_read_response)(struct efx_nic *efx, efx_dword_t *pdu,
				   size_t pdu_offset, size_t pdu_len);
	int (*mcdi_poll_reboot)(struct efx_nic *efx);
	void (*irq_enable_master)(struct efx_nic *efx);
	int (*irq_test_generate)(struct efx_nic *efx);
	void (*irq_disable_non_ev)(struct efx_nic *efx);
#if !defined(EFX_USE_KCOMPAT) || !defined(EFX_HAVE_IRQ_HANDLER_REGS)
	irqreturn_t (*irq_handle_msi)(int irq, void *dev_id);
	irqreturn_t (*irq_handle_legacy)(int irq, void *dev_id);
#else
	irqreturn_t (*irq_handle_msi)(int irq, void *dev_id, struct pt_regs *);
	irqreturn_t (*irq_handle_legacy)(int irq, void *dev_id,
					 struct pt_regs *);
#endif
	int (*tx_probe)(struct efx_tx_queue *tx_queue);
	void (*tx_init)(struct efx_tx_queue *tx_queue);
	void (*tx_remove)(struct efx_tx_queue *tx_queue);
	void (*tx_write)(struct efx_tx_queue *tx_queue);
	void (*tx_notify)(struct efx_tx_queue *tx_queue);
	int (*rx_push_rss_config)(struct efx_nic *efx, bool user,
				  const u32 *rx_indir_table);
	int (*rx_probe)(struct efx_rx_queue *rx_queue);
	void (*rx_init)(struct efx_rx_queue *rx_queue);
	void (*rx_remove)(struct efx_rx_queue *rx_queue);
	void (*rx_write)(struct efx_rx_queue *rx_queue);
	void (*rx_defer_refill)(struct efx_rx_queue *rx_queue);
	int (*ev_probe)(struct efx_channel *channel);
	int (*ev_init)(struct efx_channel *channel);
	void (*ev_fini)(struct efx_channel *channel);
	void (*ev_remove)(struct efx_channel *channel);
	int (*ev_process)(struct efx_channel *channel, int quota);
	void (*ev_read_ack)(struct efx_channel *channel);
	void (*ev_test_generate)(struct efx_channel *channel);
	int (*filter_table_probe)(struct efx_nic *efx);
	void (*filter_table_restore)(struct efx_nic *efx);
	void (*filter_table_remove)(struct efx_nic *efx);
	void (*filter_update_rx_scatter)(struct efx_nic *efx);
	s32 (*filter_insert)(struct efx_nic *efx,
			     const struct efx_filter_spec *spec, bool replace);
	int (*filter_remove_safe)(struct efx_nic *efx,
				  enum efx_filter_priority priority,
				  u32 filter_id);
	int (*filter_get_safe)(struct efx_nic *efx,
			       enum efx_filter_priority priority,
			       u32 filter_id, struct efx_filter_spec *);
	int (*filter_clear_rx)(struct efx_nic *efx,
			       enum efx_filter_priority priority);
	int (*filter_redirect)(struct efx_nic *efx, u32 filter_id,
			       int rxq_i, int stack_id);
	u32 (*filter_count_rx_used)(struct efx_nic *efx,
				    enum efx_filter_priority priority);
	u32 (*filter_get_rx_id_limit)(struct efx_nic *efx);
	s32 (*filter_get_rx_ids)(struct efx_nic *efx,
				 enum efx_filter_priority priority,
				 u32 *buf, u32 size);
#if (defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_SARFS)) || defined(CONFIG_RFS_ACCEL)
	s32 (*filter_async_insert)(struct efx_nic *efx,
				   const struct efx_filter_spec *spec);
#endif
#ifdef CONFIG_RFS_ACCEL
	bool (*filter_rfs_expire_one)(struct efx_nic *efx, u32 flow_id,
				      unsigned int index);
#endif
#if defined(EFX_NOT_UPSTREAM) && defined(EFX_USE_SARFS)
	int (*filter_async_remove)(struct efx_nic *efx, u32 filter_id);
#endif
#ifdef EFX_NOT_UPSTREAM
/* Block kernel from receiving packets except through explicit
 * configuration, i.e. remove and disable filters with priority < MANUAL
 */
	int (*filter_block_kernel)(struct efx_nic *efx,
				   enum efx_dl_filter_block_kernel_type type);
/* Unblock kernel, i.e. enable automatic and hint filters */
	void (*filter_unblock_kernel)(struct efx_nic *efx, enum
				      efx_dl_filter_block_kernel_type type);
	int (*vport_filter_insert)(struct efx_nic *efx, unsigned vport_id,
				   const struct efx_filter_spec *spec,
				   u64 *filter_id_out, bool *is_exclusive_out);
	int (*vport_filter_remove)(struct efx_nic *efx, unsigned vport_id,
				   u64 filter_id, bool is_exclusive);
#endif
#ifdef CONFIG_SFC_MTD
	int (*mtd_probe)(struct efx_nic *efx);
	void (*mtd_rename)(struct efx_mtd_partition *part);
	int (*mtd_read)(struct mtd_info *mtd, loff_t start, size_t len,
			size_t *retlen, u8 *buffer);
	int (*mtd_erase)(struct mtd_info *mtd, loff_t start, size_t len);
	int (*mtd_write)(struct mtd_info *mtd, loff_t start, size_t len,
			 size_t *retlen, const u8 *buffer);
	int (*mtd_sync)(struct mtd_info *mtd);
#endif
#ifdef CONFIG_SFC_PTP
	void (*ptp_write_host_time)(struct efx_nic *efx, u32 host_time);
	int (*ptp_set_ts_sync_events)(struct efx_nic *efx, bool en, bool temp);
	int (*ptp_set_ts_config)(struct efx_nic *efx,
				 struct hwtstamp_config *init);
#endif
	int (*sriov_init)(struct efx_nic *efx);
	void (*sriov_fini)(struct efx_nic *efx);
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_NEED_GET_PHYS_PORT_ID)
	int (*sriov_get_phys_port_id)(struct efx_nic *efx,
				      struct netdev_phys_item_id *ppid);
#endif
	bool (*sriov_wanted)(struct efx_nic *efx);
	void (*sriov_reset)(struct efx_nic *efx);
	int (*sriov_configure)(struct efx_nic *efx, int num_vfs);
	void (*sriov_flr)(struct efx_nic *efx, unsigned vf_i);
	int (*sriov_set_vf_mac)(struct efx_nic *efx, int vf_i, u8 *mac);
	int (*sriov_set_vf_vlan)(struct efx_nic *efx, int vf_i, u16 vlan,
				 u8 qos);
	int (*sriov_set_vf_spoofchk)(struct efx_nic *efx, int vf_i,
				     bool spoofchk);
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_NDO_SET_VF_MAC)
	int (*sriov_get_vf_config)(struct efx_nic *efx, int vf_i,
				   struct ifla_vf_info *ivi);
#endif
	int (*sriov_set_vf_link_state)(struct efx_nic *efx, int vf_i,
				       int link_state);
	int (*vswitching_probe)(struct efx_nic *efx);
	int (*vswitching_restore)(struct efx_nic *efx);
	void (*vswitching_remove)(struct efx_nic *efx);
	int (*get_mac_address)(struct efx_nic *efx, unsigned char *perm_addr);
	int (*set_mac_address)(struct efx_nic *efx);

	int revision;
	unsigned int txd_ptr_tbl_base;
	unsigned int rxd_ptr_tbl_base;
	unsigned int buf_tbl_base;
	unsigned int evq_ptr_tbl_base;
	unsigned int evq_rptr_tbl_base;
	u64 max_dma_mask;
	unsigned int rx_prefix_size;
	unsigned int rx_hash_offset;
	unsigned int rx_ts_offset;
	unsigned int rx_buffer_padding;
	bool can_rx_scatter;
	bool always_rx_scatter;
	bool option_descriptors;
	unsigned int min_interrupt_mode;
	unsigned int max_interrupt_mode;
	unsigned int timer_period_max;
	struct efx_dl_falcon_resources farch_resources;
	struct efx_dl_ef10_resources ef10_resources;
	struct efx_dl_hash_insertion dl_hash_insertion;
	netdev_features_t offload_features;
	int mcdi_max_ver;
	unsigned int max_rx_ip_filters;
	u32 hwtstamp_filters;
};

/* Is Driverlink supported on this device? */
static inline bool efx_dl_supported(struct efx_nic *efx)
{
	return efx->dl_info != NULL;
}

/**************************************************************************
 *
 * Prototypes and inline functions
 *
 *************************************************************************/

static inline struct efx_channel *
efx_get_channel(struct efx_nic *efx, unsigned index)
{
	EFX_BUG_ON_PARANOID(index >= efx->n_channels);
	return efx->channel[index];
}

/* Iterate over all used channels */
#define efx_for_each_channel(_channel, _efx)				\
	for (_channel = (_efx)->channel[0];				\
	     _channel;							\
	     _channel = (_channel->channel + 1 < (_efx)->n_channels) ?	\
		     (_efx)->channel[_channel->channel + 1] : NULL)

/* Iterate over all used channels in reverse */
#define efx_for_each_channel_rev(_channel, _efx)			\
	for (_channel = (_efx)->channel[(_efx)->n_channels - 1];	\
	     _channel;							\
	     _channel = _channel->channel ?				\
		     (_efx)->channel[_channel->channel - 1] : NULL)

static inline struct efx_tx_queue *
efx_get_tx_queue(struct efx_nic *efx, unsigned index, unsigned type)
{
	EFX_BUG_ON_PARANOID(index >= efx->n_tx_channels ||
			    type >= EFX_TXQ_TYPES);
	return &efx->channel[efx->tx_channel_offset + index]->tx_queue[type];
}

static inline bool efx_channel_has_tx_queues(struct efx_channel *channel)
{
	return channel->channel - channel->efx->tx_channel_offset <
		channel->efx->n_tx_channels;
}

static inline struct efx_tx_queue *
efx_channel_get_tx_queue(struct efx_channel *channel, unsigned type)
{
	EFX_BUG_ON_PARANOID(!efx_channel_has_tx_queues(channel) ||
			    type >= EFX_TXQ_TYPES);
	return &channel->tx_queue[type];
}

/* Iterate over all TX queues belonging to a channel */
#define efx_for_each_channel_tx_queue(_tx_queue, _channel)		\
	if (!efx_channel_has_tx_queues(_channel))			\
		;							\
	else								\
		for (_tx_queue = (_channel)->tx_queue;			\
		     _tx_queue < (_channel)->tx_queue + EFX_TXQ_TYPES;	\
		     _tx_queue++)

static inline bool efx_channel_has_rx_queue(struct efx_channel *channel)
{
	return channel->rx_queue.core_index >= 0;
}

static inline struct efx_rx_queue *
efx_channel_get_rx_queue(struct efx_channel *channel)
{
	EFX_BUG_ON_PARANOID(!efx_channel_has_rx_queue(channel));
	return &channel->rx_queue;
}

/* Iterate over all RX queues belonging to a channel */
#define efx_for_each_channel_rx_queue(_rx_queue, _channel)		\
	if (!efx_channel_has_rx_queue(_channel))			\
		;							\
	else								\
		for (_rx_queue = &(_channel)->rx_queue;			\
		     _rx_queue;						\
		     _rx_queue = NULL)

/* Name formats */
#define EFX_CHANNEL_NAME(_channel) "chan%d", (_channel)->channel
#define EFX_TX_QUEUE_NAME(_tx_queue) "txq%d", (_tx_queue)->queue
#define EFX_RX_QUEUE_NAME(_rx_queue) "rxq%d", efx_rx_queue_index(_rx_queue)

static inline struct efx_channel *
efx_rx_queue_channel(struct efx_rx_queue *rx_queue)
{
	return container_of(rx_queue, struct efx_channel, rx_queue);
}

static inline int efx_rx_queue_index(struct efx_rx_queue *rx_queue)
{
	return efx_rx_queue_channel(rx_queue)->channel;
}

/* Returns a pointer to the specified receive buffer in the RX
 * descriptor queue.
 */
static inline struct efx_rx_buffer *efx_rx_buffer(struct efx_rx_queue *rx_queue,
						  unsigned int index)
{
	return &rx_queue->buffer[index];
}

/**
 * EFX_MAX_FRAME_LEN - calculate maximum frame length
 *
 * This calculates the maximum frame length that will be used for a
 * given MTU.  The frame length will be equal to the MTU plus a
 * constant amount of header space and padding.  This is the quantity
 * that the net driver will program into the MAC as the maximum frame
 * length.
 *
 * The 10G MAC requires 8-byte alignment on the frame
 * length, so we round up to the nearest 8.
 *
 * Re-clocking by the XGXS on RX can reduce an IPG to 32 bits (half an
 * XGMII cycle).  If the frame length reaches the maximum value in the
 * same cycle, the XMAC can miss the IPG altogether.  We work around
 * this by adding a further 16 bytes.
 */
#define EFX_MAX_FRAME_LEN(mtu) \
	((((mtu) + ETH_HLEN + VLAN_HLEN + 4/* FCS */ + 7) & ~7) + 16)

#if !defined(EFX_USE_KCOMPAT) || (defined(EFX_HAVE_NET_TSTAMP) && defined(EFX_HAVE_SKBTX_HW_TSTAMP))
static inline bool efx_xmit_with_hwtstamp(struct sk_buff *skb)
{
	return skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP;
}
static inline void efx_xmit_hwtstamp_pending(struct sk_buff *skb)
{
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
}
#elif defined(CONFIG_SFC_PTP) && defined(EFX_HAVE_NET_TSTAMP) && !defined(EFX_HAVE_SKBTX_HW_TSTAMP)
static inline bool efx_xmit_with_hwtstamp(struct sk_buff *skb)
{
	return skb_shinfo(skb)->tx_flags.hardware;
}
static inline void efx_xmit_hwtstamp_pending(struct sk_buff *skb)
{
	skb_shinfo(skb)->tx_flags.in_progress = 1;
}
#elif defined(CONFIG_SFC_PTP)
/* No kernel timestamping: must examine the sk_buff */
static inline bool efx_xmit_with_hwtstamp(struct sk_buff *skb)
{
	return (likely(skb->protocol == htons(ETH_P_IP)) &&
		skb_transport_header_was_set(skb) &&
		skb_network_header_len(skb) >= sizeof(struct iphdr) &&
		ip_hdr(skb)->protocol == IPPROTO_UDP &&
		skb_headlen(skb) >=
		skb_transport_offset(skb) + sizeof(struct udphdr) &&
		unlikely(udp_hdr(skb)->dest == htons(319) ||
			 udp_hdr(skb)->dest == htons(320)));
}
static inline void efx_xmit_hwtstamp_pending(struct sk_buff *skb)
{
}
#else
static inline bool efx_xmit_with_hwtstamp(struct sk_buff *skb)
{
	return false;
}
static inline void efx_xmit_hwtstamp_pending(struct sk_buff *skb)
{
}
#endif

/* Get partner of a TX queue, seen as part of the same net core queue */
static inline struct efx_tx_queue *efx_tx_queue_partner(struct efx_tx_queue *tx_queue)
{
        if (tx_queue->queue & EFX_TXQ_TYPE_OFFLOAD)
                return tx_queue - EFX_TXQ_TYPE_OFFLOAD;
        else
                return tx_queue + EFX_TXQ_TYPE_OFFLOAD;
}

/* Get the given TX queue fill level */
static inline unsigned int efx_tx_queue_fill_level(struct efx_tx_queue *tx_queue)
{
	struct efx_tx_queue *txq2 = efx_tx_queue_partner(tx_queue);

	return max(tx_queue->insert_count - tx_queue->read_count,
		   txq2->insert_count - txq2->read_count);
}

#endif /* EFX_NET_DRIVER_H */
