/*************************************************************************//**
 *****************************************************************************
 * @file   stdio.h
 * @brief  
 * @author Forrest Y. Yu
 * @date   2008
 *****************************************************************************
 *****************************************************************************/

/* the assert macro */
#define ASSERT
#ifdef ASSERT
void assertion_failure(char *exp, char *file, char *base_file, int line);
#define assert(exp)  if (exp) ; \
        else assertion_failure(#exp, __FILE__, __BASE_FILE__, __LINE__)
#else
#define assert(exp)
#endif

/* EXTERN */
#define	EXTERN	extern	/* EXTERN is defined as extern except in global.c */

/* string */
#define	STR_DEFAULT_LEN	1024

#define	MAX_PATH	128

/*--------*/
/* 库函数 */
/*--------*/

/* lib/open.c */
int	open		(const char *pathname, int flags);

/* lib/close.c */
int	close		(int fd);
