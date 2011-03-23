/* header-file for dbf.c
   declares routines for reading and writing xBase-files (.dbf), and
   associated structures

   Maarten Boekhold (boekhold@cindy.et.tudelft.nl) 29 oktober 1995
*/

#ifndef _DBF_H
#define _DBF_H

#include <sys/types.h>

/**********************************************************************

		The DBF-part

***********************************************************************/

#define DBF_FILE_MODE	0644

/* byte offsets for date in dbh_date */

#define DBH_DATE_YEAR	0
#define DBH_DATE_MONTH	1
#define DBH_DATE_DAY	2

/* maximum fieldname-length */

#define DBF_NAMELEN	11

/* magic-cookies for the file */

#define DBH_NORMAL	0x03
#define DBH_MEMO	0x83

/* magic-cookies for the fields */

#define DBF_ERROR	-1
#define DBF_VALID	0x20
#define DBF_DELETED	0x2A
#define DBF_EOF         0x1A

/* Types of MEMO files; normal dbase III+,IV:DBT, FoxPro:FPT */

#define DBF_MTYPE_NONE   0
#define DBF_MTYPE_DBT3   1
#define DBF_MTYPE_DBT4   2
#define DBF_MTYPE_DBT5   3
#define DBF_MTYPE_FPT    4

#define DBF_DBT_BLOCK_SIZE 512

/* diskheader */

typedef struct {
	u_char	dbh_dbt;		/* indentification field */
	u_char	dbh_year;		/* last modification-date */
	u_char	dbh_month;
	u_char	dbh_day;
	u_char	dbh_records[4];	/* number of records */
	u_char	dbh_hlen[2];	/* length of this header */
	u_char	dbh_rlen[2];	/* length of a record */
	u_char	dbh_stub[20];	/* misc stuff we don't need */
} dbf_header;

/* disk field-description */

typedef struct {
	char	dbf_name[DBF_NAMELEN];	/* field-name terminated with \0 */
	u_char	dbf_type;		/* field-type */
	u_char	dbf_reserved[4];	/* some reserved stuff */
	u_char	dbf_flen;		/* field-length */
	u_char	dbf_dec;		/* number of decimal positions if
						       type is 'N' */
	u_char	dbf_stub[13];		/* stuff we don't need */
        u_char  dbf_isIndexed;          /* exists in index flag */
} dbf_field;

/* disk data structures for working with memo files */
typedef union {
    struct {
	u_char  next_block[4];
	u_char  reserve1[4];
	u_char  reserve2[8];
	u_char  version;
    } dbiii;
    struct {
	u_char  next_block[4];
	u_char  blocksize[4];
	u_char  filename[8];
	u_char  version;
	u_char  reserve1[3];
	u_char  blocklen[2];
    } dbiv;
    struct {
	u_char  next_block[4];
	u_char  reserve1[2];
	u_char  blocksize[2];
    } fpt;
} dbf_memo_header;

typedef struct {
    u_char	header[4];
    u_char	length[4];
} dbf_memo_block_header;

/* memory field-description */

typedef struct {
    char	db_name[DBF_NAMELEN];	/* field-name terminated with \0 */
    u_char	db_type;		/* field-type */
    u_char	db_flen;		/* field-length */
    u_char	db_dec;			/* number of decimal positions */
    u_char	db_idx;                 /* exists in index flag */
} f_descr;

/* memory dfb-header */

typedef struct {
    int		db_fd;		/* file-descriptor */
    u_long	db_offset;	/* current offset in file */
    u_char	db_memo;	/* memo-file present (and type) */
    u_char	db_year;	/* last update as YYMMDD */
    u_char	db_month;
    u_char	db_day;
    u_long	db_hlen;	/* length of the diskheader, for
				   calculating the offsets */
    u_long	db_records;	/* number of records */
    u_long	db_currec;	/* current record-number starting at 0 */
    u_short	db_rlen;	/* length of the record */
    u_char	db_nfields;	/* number of fields */
    u_char	*db_buff;	/* record-buffer to save malloc()'s */
    f_descr	*db_fields;	/* pointer to an array of field-descriptions */
    char	*db_description;	/* Pointer to string description of table type */
    int		db_memofd;		/* memo file-descriptor */
    u_char	*mb_buffer;	/* memo block buffer to save malloc()'s */    
    u_long      mb_bufsiz;	/* Current size of memo block buffer */
    u_long      mb_blksiz;	/* memo file block size */
    u_long      mb_curr_blk;	/* memo file current block number */
    u_long      mb_next_blk;	/* memo file next used block number */
    u_long      mb_free_blk;	/* memo file next free block number */
} dbhead;

/* structure that contains everything a user wants from a field, including
   the contents (in ASCII). Warning! db_flen may be bigger than the actual
   length of db_name! This is because a field doesn't have to be completely
   filled */
 
typedef struct {
    char	db_name[DBF_NAMELEN];	/* field-name terminated with \0 */
    u_char	db_type;		/* field-type */
    u_char	db_flen;		/* field-length */
    u_char	db_dec;			/* number of decimal positions */
    u_long	db_blen;		/* Length of db_contents Buffer */
    char*	db_contents;		/* contents of the field in ASCII */
} field;

/* prototypes for functions */

extern dbhead*	dbf_open(char *file ,int flags);
extern int	dbf_write_head(dbhead *dbh);
extern int	dbf_put_fields(dbhead *dbh);
extern int	dbf_add_field(dbhead *dbh, char *name, u_char type,
			      u_char length, u_char dec);
extern dbhead * dbf_open_new(char *name, int flags);
extern void	dbf_close(dbhead **dbh);
extern int	dbf_get_record(dbhead *dbh, field *fields, u_long rec);
extern field*	dbf_build_record(dbhead *dbh);
extern void	dbf_free_record(dbhead *dbh, field* fields);
extern int 	dbf_put_record(dbhead *dbh, field *rec, u_long where);

/*********************************************************************

		The endian-part

***********************************************************************/

extern long get_long(u_char *cp);
extern void put_long(u_char *cp, long lval);
extern short get_short(u_char *cp);
extern void put_short(u_char *cp, short lval);

#endif	/* _DBF_H */
