/*************************************************************************//**
 *****************************************************************************
 * @file   misc.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   2008
 *****************************************************************************
 *****************************************************************************/

/* Orange'S FS */

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "proto.h"
#include "hd.h"
#include "fs.h"

/*****************************************************************************
 *                                do_stat
 *************************************************************************//**
 * Perform the stat() syscall.
 * 
 * @return  On success, zero is returned. On error, -1 is returned.
 *****************************************************************************/
PUBLIC int do_stat()
{
	char pathname[MAX_PATH]; /* parameter from the caller */
	char filename[MAX_PATH]; /* directory has been stipped */

	/* get parameters from the message */
	int name_len = fs_msg.NAME_LEN;	/* length of filename */
	int src = fs_msg.source;	/* caller proc nr. */
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),    /* to   */
		  (void*)va2la(src, fs_msg.PATHNAME), /* from */
		  name_len);
	pathname[name_len] = 0;	/* terminate the string */

	struct fat16_stat_t stat;

	fat16_stat(&fat16, pathname, &stat);

	/* the thing requested */
	struct fat16_stat_t s;
	s.size= stat.size;

	phys_copy((void*)va2la(src, fs_msg.BUF), /* to   */
		  (void*)va2la(TASK_FS, &s),	 /* from */
		  sizeof(struct fat16_stat_t));

	return 0;
}
