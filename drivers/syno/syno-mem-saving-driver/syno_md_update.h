#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
#ifndef __SYNO_MD_UPDATE_H__
#define __SYNO_MD_UPDATE_H__

#include "flashcache.h"

// REMOVE FOR GPL RELEASE
#ifdef MY_ABC_HERE
void syno_mu_queue_no_flush_update(struct cache_c *dmc, struct kcached_job *job);
void syno_mu_queue_no_flush_fua_update(struct cache_c *dmc, struct kcached_job *job);
void syno_mu_queue_flush_update(struct cache_c *dmc, int cb_idx);
void syno_mu_queue_force_update(struct cache_c *dmc, int cb_idx);
void syno_mu_disk_flush_complete(struct work_struct *work);
void syno_mu_ssd_flush_complete(struct work_struct *work);
void syno_mu_do_md_update(struct work_struct *work);
void syno_mu_md_io_complete(struct work_struct *work);
void syno_mu_start_ext_flush(struct cache_c *dmc, struct bio *flush_bio, unsigned long start_jiffy);
void syno_mu_flush_all_synced_sync(struct cache_c *dmc);
void syno_mu_flush_all_for_remove_sync(struct cache_c *dmc);
void syno_mu_send_md_ios_from_ext_flush(struct work_struct *work);
void syno_mu_disable_dmcg_throtl(struct cache_c *dmc);
void syno_mu_dmcg_throtl(struct cache_c *dmc);

#endif /* MY_ABC_HERE */
#endif /* __SYNO_MD_UPDATE_H__ */
