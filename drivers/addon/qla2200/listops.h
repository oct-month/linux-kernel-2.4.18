/* management functions for various lists */


/* __add_to_done_queue()
 * 
 * Place SRB command on done queue.
 *
 * Input:
 *      ha           = host pointer
 *      sp           = srb pointer.
 * Locking:
 * 	this function assumes the ha->list_lock is already taken
 */
static inline void __add_to_done_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/* Place block on done queue */
	list_add_tail(&sp->list,&ha->done_queue);
	ha->done_q_cnt++;
}

static inline void __add_to_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/* Place block on retry queue */
	list_add_tail(&sp->list,&ha->retry_queue);
	qla2100_stats.retry_q_cnt++;
	sp->flags |= SRB_WATCHDOG;
	ha->flags.watchdog_enabled = TRUE;
}

/* add_to_done_queue()
 * 
 * Place SRB command on done queue.
 *
 * Input:
 *      ha           = host pointer
 *      sp           = srb pointer.
 * Locking:
 * 	this function takes and releases the ha->list_lock
 */
static inline void add_to_done_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_done_queue(ha,sp);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}


static inline void add_to_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_retry_queue(ha,sp);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*
 * __del_from_retry_queue
 *      Function used to remove a command block from the
 *      watchdog timer queue.
 *
 *      Note: Must insure that command is on watchdog
 *            list before calling del_from_retry_queue
 *            if (sp->flags & SRB_WATCHDOG)
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function assumes the list_lock is already taken
 */
static inline void __del_from_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	list_del_init(&sp->list);

	if (list_empty(&ha->retry_queue))
		ha->flags.watchdog_enabled = FALSE;
	sp->wdg_time = 0;
	sp->flags &= ~(SRB_WATCHDOG | SRB_BUSY);
	qla2100_stats.retry_q_cnt--;
}

static inline void __del_from_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	list_del_init(&sp->list);
}

static inline void __del_from_cmd_queue(struct os_lun *lq, srb_t * sp)
{
	lq->q_incnt--;
	list_del_init(&sp->list);
}


/*
 * del_from_retry_queue
 *      Function used to remove a command block from the
 *      watchdog timer queue.
 *
 *      Note: Must insure that command is on watchdog
 *            list before calling del_from_retry_queue
 *            if (sp->flags & SRB_WATCHDOG)
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function takes and releases the list_lock
 */
static inline void del_from_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	unsigned long flags;

/*	if (unlikely(!(sp->flags & SRB_WATCHDOG)))
		BUG();*/
	spin_lock_irqsave(&ha->list_lock, flags);

/*	if (unlikely(list_empty(&ha->retry_queue)))
		BUG();*/

	__del_from_retry_queue(ha,sp);

	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void del_from_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);

	__del_from_failover_queue(ha,sp);

	spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*
 * qla2100_putq_b
 *      Add the standard SCB job to the bottom of standard SCB commands.
 *
 * Input:
 *      q  = SCSI LU pointer.
 *      sp = srb pointer.
 *      SCSI_LU_Q lock must be already obtained.
 */
static inline void __add_to_cmd_queue(struct os_lun * lq, srb_t * sp) 
{
	list_add_tail(&sp->list,&lq->cmd);
	lq->q_incnt++;
}

static inline void __add_to_cmd_queue_head(struct os_lun * lq, srb_t * sp) 
{
	list_add(&sp->list,&lq->cmd);
	lq->q_incnt++;
}

static inline void add_to_cmd_queue(struct scsi_qla_host * ha, struct os_lun * lq, srb_t * sp) 
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_cmd_queue(lq,sp);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*
 * del_from_cmd_queue
 *      Function used to remove a command block from the
 *      LU queue.
 *
 * Input:
 *      q  = SCSI LU pointer.
 *      sp = srb pointer.
 *      SCSI_LU_Q lock must be already obtained.
 */
static inline void del_from_cmd_queue(struct scsi_qla_host * ha, struct os_lun * lq, srb_t * sp) 
{
	unsigned long flags;
	spin_lock_irqsave(&ha->list_lock, flags);
	__del_from_cmd_queue(lq,sp);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}
