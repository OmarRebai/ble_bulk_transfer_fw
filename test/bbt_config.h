#ifndef BBT_CONFIG_H
#define BBT_CONFIG_H

#define BBT_OS_BAREMETAL

#define BBT_MAX_CHUNK_SIZE        244u
#define BBT_MAX_TOTAL_CHUNKS      4096u
#define BBT_RX_QUEUE_DEPTH        16u
#define BBT_RX_THREAD_STACK_SIZE  1024u
#define BBT_RX_THREAD_PRIORITY    1u
#define BBT_WORKER_POLL_MS        20u
#define BBT_IDLE_TIMEOUT_MS       5000u
#define BBT_NACK_RETRY_MAX        3u
#define BBT_MAX_NOTIFY_SIZE       260u

#endif