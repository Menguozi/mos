/*************************************************************************//**
 *****************************************************************************
 * @file   read_write.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   2008
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
 *                                do_rdwt
 *****************************************************************************/
/**
 * Read/Write file and return byte count read/written.
 *
 * Sector map is not needed to update, since the sectors for the file have been
 * allocated and the bits are set when the file was created.
 * 
 * @return How many bytes have been read/written.
 *****************************************************************************/
PUBLIC int do_rdwt()
{
	int fd = fs_msg.FD;	/**< file descriptor. */
	void * buf = fs_msg.BUF;/**< r/w buffer */
	int len = fs_msg.CNT;	/**< r/w bytes */

	int src = fs_msg.source;		/* caller proc nr. */

	assert((pcaller->filp[fd] >= &f_desc_table[0]) &&
	       (pcaller->filp[fd] < &f_desc_table[NR_FILE_DESC]));

	if (!(pcaller->filp[fd]->fd_mode & O_RDWR))
		return 0;

	struct fat16_file_t *file_p = pcaller->filp[fd]->fd_file;

	if (fd < 2) {
		int t = fs_msg.type == READ ? DEV_READ : DEV_WRITE;
		fs_msg.type = t;

		int dev = MAKE_DEV(DEV_CHAR_TTY, 0);
		assert(MAJOR(dev) == 4);

		fs_msg.DEVICE	= MINOR(dev);
		fs_msg.BUF	= buf;
		fs_msg.CNT	= len;
		fs_msg.PROC_NR	= src;
		assert(dd_map[MAJOR(dev)].driver_nr != INVALID_DRIVER);
		send_recv(BOTH, dd_map[MAJOR(dev)].driver_nr, &fs_msg);
		assert(fs_msg.CNT == len);

		return fs_msg.CNT;
	}
	else {
		assert((fs_msg.type == READ) || (fs_msg.type == WRITE));

		int bytes_rw = 0;

		if (fs_msg.type == READ) {
			//printl("pos: %d\n",file_p->cur_position);
			fat16_file_seek(file_p, 0, 0);

			int size = file_p->file_size;
			int i;
			for(i=0;i<(size+511)/512;i++)
			{
				bytes_rw += fat16_file_read(file_p, fsbuf+i*512, 512);
				phys_copy((void*)va2la(src, buf+i*512),
				  	(void*)va2la(TASK_FS, fsbuf+i*512),
				  	len);
			}
			
			fsbuf[len]=0;
		}
		else {	/* WRITE */
			char wbuf[10000];
			phys_copy((void*)va2la(TASK_FS, wbuf),
				  (void*)va2la(src, buf),
				  len);
			bytes_rw = fat16_file_write(file_p, wbuf, len);
			fat16_file_sync(file_p);
		}

		return bytes_rw;
	}
}
