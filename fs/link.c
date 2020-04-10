/*************************************************************************//**
 *****************************************************************************
 * @file   link.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   Tue Jun  3 17:05:10 2008
 *****************************************************************************
 *****************************************************************************/

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


/*****************************************************************************
 *                                do_unlink
 *****************************************************************************/
/**
 * Remove a file.
 *
 * @note We clear the i-node in inode_array[] although it is not really needed.
 *       We don't clear the data bytes so the file is recoverable.
 * 
 * @return On success, zero is returned.  On error, -1 is returned.
 *****************************************************************************/
PUBLIC int do_unlink()
{
	char pathname[MAX_PATH];

	/* get parameters from the message */
	int name_len = fs_msg.NAME_LEN;	/* length of filename */
	int src = fs_msg.source;	/* caller proc nr. */
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),
		  (void*)va2la(src, fs_msg.PATHNAME),
		  name_len);
	pathname[name_len] = 0;

	return 0;
}
