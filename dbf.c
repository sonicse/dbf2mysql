/* Routines to read and write xBase-files (.dbf)

   By Maarten Boekhold, 29th of oktober 1995

   Modified by Frank Koormann (fkoorman@usf.uni-osnabrueck.de), Jun 10 1996
	prepare dataarea with memset
	get systemtime and set filedate
	set formatstring for real numbers

   Mod by Bob Schulze (bob@yipp.de)
   Added Handling for DBF D (Date) fields. 
*/

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include "dbf.h"

static int dbf_get_memo_data(dbhead *dbh, field *fldp,  u_long blknum);

void cp866_cp1251( char* s ) {
    char *c;
    for( c=s; *c!=0; ++c ) {
        if( *c>-129 && *c<-80 )
            *c += 64;
        else if( *c>-33 && *c<-16 )
            *c += 16;
    }
}

struct _dbf_file_signatures {
    unsigned char	sig;
    unsigned char	hasMemo;
    unsigned char	indexValid;
    char	 	*description;
};

static struct _dbf_file_signatures valid_dbf_types[] = 
{
     {0x03,DBF_MTYPE_NONE,0,"dBASE III w/o memo file"},
     {0x04,DBF_MTYPE_NONE,1,"dBASE IV or IV w/o memo file"},
     {0x05,DBF_MTYPE_NONE,1,"dBASE V w/o memo file"},
     {0x83,DBF_MTYPE_DBT3,0,"dBASE III+ with memo file"},
     {0xF5,DBF_MTYPE_FPT ,0,"FoxPro w. memo file"},
     {0x8B,DBF_MTYPE_DBT4,1,"dBASE IV w. memo"},
     {0x8E,DBF_MTYPE_NONE,1,"dBASE IV w. SQL table"},
     {0x30,DBF_MTYPE_FPT ,1,"Visual FoxPro w. DBC"},
     {0x7B,DBF_MTYPE_DBT4,1,"dBASE IV with memo"}
};

static u_char days_in_month[]= {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* open a dbf-file, get it's field-info and store this information */

dbhead *dbf_open(char *file, int flags)
{
    int			file_no;
    dbhead		*dbh;
    f_descr		*fields;
    dbf_header		*head;
    dbf_field		*fieldc;
    int			t;
    int			i;
    char		*sp;
    int			memofound;
    dbf_memo_header	*dbmh;

    if ((dbh = (dbhead *)calloc(1, sizeof(dbhead))) == NULL) {
	return (dbhead *)DBF_ERROR;
    }

    if ((head = (dbf_header *)malloc(sizeof(dbf_header))) == NULL) {
	free(dbh);
	return (dbhead *)DBF_ERROR;
    }

    if ((fieldc = (dbf_field *)malloc(sizeof(dbf_field))) == NULL) {
	free(head);
	free(dbh);
	return (dbhead *)DBF_ERROR;
    }

    if ((file_no = open(file, flags)) == -1) {
	free(fieldc);
	free(head);
	free(dbh);
	return (dbhead *)DBF_ERROR;
    }

/* read in the disk-header */

    if (read(file_no, head, sizeof(dbf_header)) == -1) {
	close(file_no);
	free(fieldc);
	free(head);
	free(dbh);
	return (dbhead *)DBF_ERROR;
    }

    for (t = 0; t < sizeof(valid_dbf_types)/sizeof(valid_dbf_types[0]); t++)
	if (valid_dbf_types[t].sig == head->dbh_dbt)
	    break;

    if (t == sizeof(valid_dbf_types)/sizeof(valid_dbf_types[0])) {
	close(file_no);
	fprintf(stderr,"Header value %02x not valid\n", head->dbh_dbt);
	free(fieldc);
	free(head);
	free(dbh);
	return (dbhead *)DBF_ERROR;
    }

    dbh->db_description = valid_dbf_types[t].description;

    if (valid_dbf_types[t].hasMemo != DBF_MTYPE_NONE) {
	dbh->db_memo = valid_dbf_types[t].hasMemo;
    } else {
	dbh->db_memo = 0;
    }
    dbh->db_fd = file_no;
    dbh->db_year = head->dbh_year;
    dbh->db_month = head->dbh_month;
    dbh->db_day = head->dbh_day;
    dbh->db_hlen = get_short((u_char *)&head->dbh_hlen);
    dbh->db_records = get_long((u_char *)&head->dbh_records);
    dbh->db_currec = 0;
    dbh->db_rlen = get_short((u_char *)&head->dbh_rlen);
    dbh->db_nfields = (dbh->db_hlen - sizeof(dbf_header)) / sizeof(dbf_field);

    if ((dbh->db_buff = (u_char *)malloc(dbh->db_rlen)) == NULL) {
	close(file_no);
	free(fieldc);
	free(head);
	free(dbh);
	return (dbhead *)DBF_ERROR;
    }

    /* dbh->db_hlen - sizeof(dbf_header) isn't the
       correct size, cos dbh->hlen is in fact
       a little more cos of the 0x0D (and
       possibly another byte, 0x4E, I have
       seen this somewhere). Because of rounding
       everything turns out right :) */

    if ((fields = (f_descr *)calloc(dbh->db_nfields, sizeof(f_descr))) == NULL)
    {
	close(file_no);
	free(fieldc);
	free(head);
	free(dbh);
	return (dbhead *)DBF_ERROR;
    }

    memofound = 0;
    for (t = 0; t < dbh->db_nfields; t++)
    {
	read(file_no, fieldc, sizeof(dbf_field));
	if (fieldc->dbf_type == 0x00)
	{
	    dbh->db_nfields = t;
	    break;
	}
	strncpy(fields[t].db_name, fieldc->dbf_name, DBF_NAMELEN);
	fields[t].db_type = fieldc->dbf_type;	
	fields[t].db_flen = fieldc->dbf_flen;
	fields[t].db_dec  = fieldc->dbf_dec;
	fields[t].db_idx  = fieldc->dbf_isIndexed;
	if (fields[t].db_type == 'M')
	    memofound++;
    }

    dbh->db_offset = dbh->db_hlen;
    dbh->db_fields = fields;

    free(fieldc);
    free(head);

    if (memofound == 0)
    {
	dbh->db_memo = DBF_MTYPE_NONE;
    }
    else
    {
	dbh->mb_blksiz = DBF_DBT_BLOCK_SIZE;
	dbh->mb_bufsiz = dbh->mb_blksiz * 2;
	if ((dbh->mb_buffer = malloc(dbh->mb_bufsiz)) == NULL)
	{
	    perror("malloc");
	    dbf_close(&dbh);
	    return (dbhead *)DBF_ERROR;
	}
	dbmh = (dbf_memo_header *)dbh->mb_buffer;
	sp = strdup(file);
	i = strlen(sp)-1;
	switch (dbh->db_memo)
	{
	  case DBF_MTYPE_FPT:
	    if (isupper(sp[i]))
		strcpy(&sp[i-2],"FPT");
	    else
		strcpy(&sp[i-2],"fpt");
	    if ((dbh->db_memofd = open(sp, flags)) == -1)
	    {
		if (isupper(sp[i]))
		    strcpy(&sp[i-2],"fpt");
		else
		    strcpy(&sp[i-2],"FPT");
		if ((dbh->db_memofd = open(sp, flags)) == -1)
		{
		    char	msg[256];
		    int	saverrno = errno;

		    if (isupper(sp[i]))
			strcpy(&sp[i-2],"fpt");
		    else
			strcpy(&sp[i-2],"FPT");
		    snprintf(msg, sizeof(msg),
			     "couldn't find required memo data file: %s - (%d)",
			     sp,
			     saverrno);
		    msg[sizeof(msg)-1] = 0;
		    errno = saverrno;
		    perror(msg);
		    dbf_close(&dbh);
		    free(sp);
		    return (dbhead *)DBF_ERROR;
		}
	    }
	    if (read(dbh->db_memofd, dbh->mb_buffer, sizeof(dbf_memo_header)) != sizeof(dbf_memo_header))
	    {
		perror("memo file read");
		dbf_close(&dbh);
		free(sp);
		return (dbhead *)DBF_ERROR;
	    }
	    dbh->mb_next_blk = (dbmh->fpt.next_block[0]<<24) + (dbmh->fpt.next_block[1]<<16) +
		(dbmh->fpt.next_block[2]<<8) + (dbmh->fpt.next_block[3]<<0);
	    dbh->mb_blksiz = (dbmh->fpt.blocksize[0]<<8) + dbmh->fpt.blocksize[1];
#if 0
	    fprintf(stderr, "Visual FoxPro memo file block size of %lu, next %lu\n", dbh->mb_blksiz,dbh->mb_next_blk);
#endif
	    if (dbh->mb_blksiz > dbh->mb_bufsiz)
	    {
		int	newsiz = (((dbh->mb_blksiz * 2 + 4095)/4096)+1)*4096-16;
		void	*tmpptr = realloc(dbh->mb_buffer, newsiz);
		if (tmpptr != NULL)
		{
		    dbh->mb_buffer = tmpptr;
		    dbh->mb_bufsiz = newsiz;
		}
	    }
	    break;

	  case DBF_MTYPE_DBT3:
	  case DBF_MTYPE_DBT4:
	    if (isupper(sp[i]))
		strcpy(&sp[i-2],"DBT");
	    else
		strcpy(&sp[i-2],"dbt");
	    if ((dbh->db_memofd = open(sp, flags)) == -1)
	    {
		if (isupper(sp[i]))
		    strcpy(&sp[i-2],"dbt");
		else
		    strcpy(&sp[i-2],"DBT");
		if ((dbh->db_memofd = open(sp, flags)) == -1)
		{
		    char	msg[256];
		    int	saverrno = errno;

		    if (isupper(sp[i]))
			strcpy(&sp[i-2],"dbt");
		    else
			strcpy(&sp[i-2],"DBT");
		    snprintf(msg, sizeof(msg),
			     "couldn't find required memo data file: %s - (%d)",
			     sp,
			     saverrno);
		    msg[sizeof(msg)-1] = 0;
		    errno = saverrno;
		    perror(msg);
		    dbf_close(&dbh);
		    free(sp);
		    return (dbhead *)DBF_ERROR;
		}
	    }
	    if (read(dbh->db_memofd, dbh->mb_buffer, sizeof(dbf_memo_header)) != sizeof(dbf_memo_header))
	    {
		perror("memo file read");
		dbf_close(&dbh);
		free(sp);
		return (dbhead *)DBF_ERROR;
	    }

	    /* Visual FoxPro V6.0 when told to write a DB IV format actually
	       creates a DB III+ file however leaves the version field zero
	       like a DB IV one, try to account for this */
	    if (dbmh->dbiii.version == 0x00 &&
		get_short(dbmh->dbiv.blocklen) == 0 &&
		dbmh->dbiv.filename[0] == '\0')
		dbmh->dbiii.version = 0x03;

	    switch (dbmh->dbiii.version)
	    {
	      case 0x03:
		dbh->db_memo = DBF_MTYPE_DBT3; /* Set the real type */
		dbh->mb_next_blk = get_long(dbmh->dbiii.next_block);
		dbh->mb_blksiz = DBF_DBT_BLOCK_SIZE;
#if 0
		fprintf(stderr, "DB III memo file block size of %lu, next %lu\n", dbh->mb_blksiz, dbh->mb_next_blk);
#endif
		break;

	      case 0x00:
		dbh->db_memo = DBF_MTYPE_DBT4; /* Set the real type */
		dbh->mb_next_blk = get_long(dbmh->dbiv.next_block);
		dbh->mb_blksiz = get_short(dbmh->dbiv.blocklen);
#if 0
		fprintf(stderr, "DB IV memo file block size of %lu, next %lu\n", dbh->mb_blksiz, dbh->mb_next_blk);
#endif
	      break;

	      default:
		fprintf(stderr,"DB III+/DB IV bad memo file version %02x\n",
			dbmh->dbiii.version);
		dbf_close(&dbh);
		free(sp);
		return (dbhead *)DBF_ERROR;
	    }
	    
	    if (dbh->mb_blksiz > dbh->mb_bufsiz)
	    {
		int	newsiz = (((dbh->mb_blksiz * 2 + 4095)/4096)+1)*4096-16;
		void	*tmpptr = realloc(dbh->mb_buffer, newsiz);
		if (tmpptr != NULL)
		{
		    dbh->mb_buffer = tmpptr;
		    dbh->mb_bufsiz = newsiz;
		}
	    }
	    break;

	  case DBF_MTYPE_NONE:
	  default:
	    fprintf(stderr,"Found memo field in table of type (%s) which isn't supported yet\n",
		    dbh->db_description);
	    dbf_close(&dbh);
	    free(sp);
	    return (dbhead *)DBF_ERROR;
	    break;
	}
	free(sp);
    }
    
    
    return dbh;
}

int	dbf_write_head(dbhead *dbh) {
	dbf_header	head;
	time_t now;
	struct tm *dbf_time;

	if (lseek(dbh->db_fd, 0, SEEK_SET) == -1) {
		return DBF_ERROR;
	}

/* fill up the diskheader */

/* Set dataarea of head to '\0' */
	memset(&head,'\0',sizeof(dbf_header));

	head.dbh_dbt = DBH_NORMAL;
	if (dbh->db_memo) head.dbh_dbt = DBH_MEMO;

	now = time((time_t *)NULL); 
	dbf_time = localtime(&now);
	head.dbh_year = dbf_time->tm_year;
	head.dbh_month = dbf_time->tm_mon + 1; /* Months since January + 1 */
	head.dbh_day = dbf_time->tm_mday;

	put_long(head.dbh_records, dbh->db_records);
	put_short(head.dbh_hlen, dbh->db_hlen);
	put_short(head.dbh_rlen, dbh->db_rlen);
	
	if (write(dbh->db_fd, &head, sizeof(dbf_header)) == -1 ) {
		return DBF_ERROR;
	}

	return 0;
}

int dbf_put_fields(dbhead *dbh) {
	dbf_field	field;
	u_long		t;
	u_char		end = 0x0D;

	if (lseek(dbh->db_fd, sizeof(dbf_header), SEEK_SET) == -1) {
		return DBF_ERROR;
	}

/* Set dataarea of field to '\0' */
	memset(&field,'\0',sizeof(dbf_field));

	for (t = 0; t < dbh->db_nfields; t++) {
		strncpy(field.dbf_name, dbh->db_fields[t].db_name, DBF_NAMELEN - 1);
		field.dbf_type = dbh->db_fields[t].db_type;
		field.dbf_flen = dbh->db_fields[t].db_flen;
		field.dbf_dec = dbh->db_fields[t].db_dec;

		if (write(dbh->db_fd, &field, sizeof(dbf_field)) == -1) {
			return DBF_ERROR;
		}
	}

	if (write(dbh->db_fd, &end, 1) == -1) {
		return DBF_ERROR;
	}

	return 0;
}

int dbf_add_field(dbhead *dbh, char *name, u_char type,
		  u_char length, u_char dec) {
f_descr	*ptr;
u_char	*foo;
u_long	size, field_no;

	size = (dbh->db_nfields + 1) * sizeof(f_descr);
	if (!(ptr = (f_descr *) realloc(dbh->db_fields, size))) {
		return DBF_ERROR;
	}
	dbh->db_fields = ptr;

	field_no = dbh->db_nfields;
	strncpy(dbh->db_fields[field_no].db_name, name, DBF_NAMELEN);
	dbh->db_fields[field_no].db_type = type;
	dbh->db_fields[field_no].db_flen = length;
	dbh->db_fields[field_no].db_dec = dec;

	dbh->db_nfields++;
	dbh->db_hlen += sizeof(dbf_field);
	dbh->db_rlen += length;

	if (!(foo = (u_char *) realloc(dbh->db_buff, dbh->db_rlen))) {
		return DBF_ERROR;
	}

	dbh->db_buff = foo;

	return 0;
}

dbhead *dbf_open_new(char *name, int flags) {
dbhead	*dbh;

	if (!(dbh = (dbhead *)malloc(sizeof(dbhead)))) {
		return (dbhead *)DBF_ERROR;
	}

	if (flags & O_CREAT) {
		if ((dbh->db_fd = open(name, flags, DBF_FILE_MODE)) == -1) {
			free(dbh);
			return (dbhead *)DBF_ERROR;
		}
	} else {
		if ((dbh->db_fd = open(name, flags)) == -1) {
			free(dbh);
			return (dbhead *)DBF_ERROR;
		}
	}
		

	dbh->db_offset = 0;
	dbh->db_memo = 0;
	dbh->db_year = 0;
	dbh->db_month = 0;
	dbh->db_day	= 0;
	dbh->db_hlen = sizeof(dbf_header) + 1;
	dbh->db_records = 0;
	dbh->db_currec = 0;
	dbh->db_rlen = 1;
	dbh->db_nfields = 0;
	dbh->db_buff = NULL;
	dbh->db_fields = (f_descr *)NULL;

	return dbh;
}
	
void dbf_close(dbhead **dbhp)
{
    close((*dbhp)->db_fd);

    free((*dbhp)->db_fields);
    (*dbhp)->db_fields = NULL;	/* Defend against getting called twice */

    free((*dbhp)->db_buff);
    (*dbhp)->db_buff = NULL;

    if ((*dbhp)->db_memo != DBF_MTYPE_NONE)
    {
	/* since memo files aren't on stdin this works */
	if ((*dbhp)->db_memofd > 0)
	    close((*dbhp)->db_memofd);
	(*dbhp)->db_memofd = 0;
	free((*dbhp)->mb_buffer);
	(*dbhp)->mb_buffer = NULL;
	(*dbhp)->mb_bufsiz = 0;
    }

    free(*dbhp);
    *dbhp = NULL;
}
	
int dbf_get_record(dbhead *dbh, field *fields,  u_long rec)
{
    int     	t, i;
    u_char  	*dbffield;
    char	*end, *sp;
    double	dblval;
    long long	int8val;
    long	int4val;
    u_int	hh, mm, ss, month, day, year;
    u_int	daynum, leap_day, day_of_year, days_in_year;
    u_char 	*days_per_month;
    u_long	blknum;

/* calculate at which offset we have to read. *DON'T* forget the
   0x0D which seperates field-descriptions from records!

   Note (april 5 1996): This turns out to be included in db_hlen
*/
    i = dbh->db_hlen + (rec * dbh->db_rlen);

    if (lseek(dbh->db_fd, i, SEEK_SET) == -1) {
	lseek(dbh->db_fd, 0, SEEK_SET);
	dbh->db_offset = 0;
	return DBF_ERROR;
    }

    dbh->db_offset 	= i;
    dbh->db_currec	= rec;

    read(dbh->db_fd, dbh->db_buff, dbh->db_rlen);

    if (dbh->db_buff[0] == DBF_DELETED) {
	return DBF_DELETED;
    }

    dbffield = &dbh->db_buff[1];
    for (t = 0; t < dbh->db_nfields; t++)
    {
        switch(fields[t].db_type) {
          case 'C':		/* Character data */
	    end = (char *)&dbffield[fields[t].db_flen - 1];
	    i = fields[t].db_flen;

            //2011-03-23 AA: Skip only 0-21 code character
	    while (( i > 0) && ((*end < 0x21) && (*end >= 0))) {
		end--;
		i--;
	    }
            strncpy(fields[t].db_contents, (char *)dbffield, i);
	    fields[t].db_contents[i] = '\0';
	    break;

	  case 'B':		/* Binary data - 8 byte field - ???Endian Alert??? */
	    memcpy(&dblval, dbffield, sizeof(dblval));
	    snprintf(fields[t].db_contents, fields[t].db_blen,  "%.*f", fields[t].db_dec, dblval);
	    fields[t].db_contents[fields[t].db_blen-1] = '\0';
	    break;

          case 'M':		/* Memo field */
          case 'G':		/* OLE field */
	    switch(dbh->db_memo)
	    {
	      case DBF_MTYPE_FPT:
		blknum = get_long(dbffield);
		break;
	      case DBF_MTYPE_DBT3:
	      case DBF_MTYPE_DBT4:
		strncpy(fields[t].db_contents, (char *)dbffield, fields[t].db_flen);
		fields[t].db_contents[fields[t].db_flen] = '\0';
		blknum = atol(fields[t].db_contents);
		memset(fields[t].db_contents, 0, fields[t].db_flen);
		break;
	      default:
		blknum = 0;
	    }
	    if (blknum != 0)
		dbf_get_memo_data(dbh, &fields[t], blknum);
	    else
		memset(fields[t].db_contents, '\0', fields[t].db_blen);
	    break;

	  case 'Y':		/* Currency 8 byte field - ???Endian Alert??? */
	    memcpy(&int8val, dbffield, sizeof(int8val));
	    snprintf(fields[t].db_contents, fields[t].db_blen,  "%0*Ld",
		     fields[t].db_dec+1, int8val);
	    fields[t].db_contents[fields[t].db_blen-1] = '\0';
	    end = fields[t].db_contents + strlen(fields[t].db_contents) + 1;
	    *end-- = '\0';
	    sp = end - 1;
	    for (i=0; i<fields[t].db_dec; i++)
		*end-- = *sp--;
	    *++sp = '.';
	    break;

	  case 'T':		/* Time 8 byte field - ???Endian Alert??? */
	    memcpy(&daynum, &dbffield[0], sizeof(daynum));
	    daynum -= 1721060;	/* Magic number for 0000-01-01 */
	    if (daynum <= 365 || daynum >= 3652500)
	    {			/* Only allow 0001-01-01 through 9999-12-31 */
	      year = month = day = 0;
	    }
	    else
	    {
	      year = daynum * 100 / 36525;
	      day_of_year = (daynum - year * 365) - (year-1) / 4 +
		  (((year - 1) / 100 + 1) * 3) / 4;
	      while (day_of_year >
		     (days_in_year =
		      ((year & 3) == 0 &&
		       (year % 100 ||
			(year % 400 == 0 && year)) ? 366 : 365)))
	      {
		day_of_year -= days_in_year;
		year++;
	      }
	      leap_day = 0;
	      if (days_in_year == 366)
	      {
		if (day_of_year > 31 + 28)
		{
		  day_of_year--;
		  if (day_of_year == 31 + 28)
		    leap_day = 1;		/* Handle leapyear's leapday */
		}
	      }
	      month = 1;
	      days_per_month = days_in_month;
	      while (day_of_year > *days_per_month)
	      {
		  month++;
		  day_of_year -= *days_per_month++;
	      }
	      day = day_of_year + leap_day;
	    }
	    memcpy(&int4val, &dbffield[4], sizeof(int4val));
	    int4val = int4val / 1000; /* Discard millseconds */
	    ss = int4val % 60;
	    mm = (int4val / 60) % 60;
	    hh = int4val / (60 * 60);
	    snprintf(fields[t].db_contents, fields[t].db_blen,
		     "%04d-%02d-%02d %02d:%02d:%02d",
		     year, month, day, hh, mm, ss);
	    fields[t].db_contents[fields[t].db_blen-1] = '\0';
	    break;

	  case 'I':		/* Integer 8 byte field - ???Endian Alert??? */
	    memcpy(&int4val, dbffield, sizeof(int4val));
	    snprintf(fields[t].db_contents, fields[t].db_blen,  "%ld", int4val);
	    fields[t].db_contents[fields[t].db_blen-1] = '\0';
	    break;

	  default:
	    end = (char *)dbffield;
	    i = fields[t].db_flen;
	    while (( i > 0) && ((*end < 0x21)/* || (*end > 0x7E)*/)) {
		end++;
		i--;
	    }
	    strncpy(fields[t].db_contents, end, i);
	    fields[t].db_contents[i] = '\0';
	    break;
	}
	dbffield += fields[t].db_flen;
    }

    dbh->db_offset += dbh->db_rlen;

    return DBF_VALID;
}

field *dbf_build_record(dbhead *dbh)
{
    int 	t;
    field	*fields;
  
    if (!(fields = (field *)calloc(dbh->db_nfields, sizeof(field)))) {
	return (field *)DBF_ERROR;
    }
  
    for ( t = 0; t < dbh->db_nfields; t++)
    {
	switch(dbh->db_fields[t].db_type)
	{
	  case 'M':
	    fields[t].db_blen = dbh->mb_blksiz + 1;
	    break;

	  case 'B':
	  case 'Y':
	    fields[t].db_blen = 26;
	    break;

	  case 'T':
	    fields[t].db_blen = 24;
	    break;

	  case 'I':
	    fields[t].db_blen = 16;
	    break;

	  case 'G':
	  case 'P':
	    fields[t].db_blen = dbh->mb_blksiz * 2 + 2; /* Binary to hex representation */
	    break;

	  default:
	    fields[t].db_blen = dbh->db_fields[t].db_flen + 1;
	}

	if (!(fields[t].db_contents = (char *)malloc(fields[t].db_blen)))
	{
	    int		i;
	    for (i = 0; i < t; i++)
		if (fields[i].db_contents != 0)
		    free(fields[i].db_contents);
	    free(fields);
	    return (field *)DBF_ERROR;
	}
	strncpy(fields[t].db_name, dbh->db_fields[t].db_name, DBF_NAMELEN);
	fields[t].db_type = dbh->db_fields[t].db_type;
	//printf("TYPE: %d NAME: %s LEN: %d\n",dbh->db_fields[t].db_type, dbh->db_fields[t].db_name,dbh->db_fields[t].db_flen); 
	fields[t].db_flen = dbh->db_fields[t].db_flen;
	fields[t].db_dec  = dbh->db_fields[t].db_dec;
    } 
    return fields;
}

void dbf_free_record(dbhead *dbh, field *rec) {
	int t;

	for ( t = 0; t < dbh->db_nfields; t++) {

	  free(rec[t].db_contents);

	}

	free(rec);
}

int dbf_put_record(dbhead *dbh, field *rec, u_long where) {
  u_long	offset, new, idx, t, h, length;
  u_char	*data;
  double	fl;
  char		foo[128], format[32];

  int 		i,j;                        // for type D
  char 		newdate[10];

  /*	offset:	offset in file for this record
	new:	real offset after lseek
	idx:	index to which place we are inside the 'hardcore'-data for this
	record
	t:		field-counter
	data:	the hardcore-data that is put on disk
	h:		index into the field-part in the hardcore-data
	length:	length of the data to copy
	fl:		a float used to get the right precision with real numbers
	foo:	copy of db_contents when field is not 'C'
	format:	sprintf format-string to get the right precision with real numbers
	
	NOTE: this declaration of 'foo' can cause overflow when the contents-field
	is longer the 127 chars (which is highly unlikely, cos it is not used
	in text-fields).
  */


  if (where > dbh->db_records) {
    if ((new = lseek(dbh->db_fd, 0, SEEK_END)) == -1) {
      return DBF_ERROR;
    }
    dbh->db_records++;
  } else {
    offset = dbh->db_hlen + (where * dbh->db_rlen);
    if ((new = lseek(dbh->db_fd, offset, SEEK_SET)) == -1) {
      return DBF_ERROR;
    }
  }
  
  dbh->db_offset = new;
  
  data = dbh->db_buff;

  /* Set dataarea of data to ' ' (space) */
  memset(data,' ',dbh->db_rlen);
 
  data[0] = DBF_VALID;
 
  idx = 1;
  for (t = 0; t < dbh->db_nfields; t++) {

    /* if field is empty, don't do a thing */
    if (rec[t].db_contents[0] != '\0') {
      /*	Handle text */
      if (rec[t].db_type == 'C') {

	if (strlen(rec[t].db_contents) > rec[t].db_flen) {
	  length = rec[t].db_flen;
	} else {
	  length = strlen(rec[t].db_contents);
	}
	strncpy((char *)&data[idx], rec[t].db_contents, length);
      } else if (rec[t].db_type == 'D' ) {
	/* handle date fields: make 1999-12-31...to 19991231 **/
	j = 0;
	for (i = 0; i  < 10; i++) {
	  if (rec[t].db_contents[i] != '-') {
	    newdate[j++] = rec[t].db_contents[i];
	  }
	}
	newdate[j++] = '\000';
	//printf("DATE: %s -> %s\n",rec[t].db_contents, newdate );
	strncpy((char *)&data[idx], newdate, 8);
      } else {
	/* Handle the rest */
	
	/* Numeric is special, because of real numbers */
	if ((rec[t].db_type == 'N') && (rec[t].db_dec != 0)) {

	  fl = atof(rec[t].db_contents);
	  sprintf(format, "%%.%df", rec[t].db_dec); 
	  sprintf(foo, format, fl);
	} else {
	  strcpy(foo, rec[t].db_contents);
	}
	if (strlen(foo) > rec[t].db_flen) {
	  length = rec[t].db_flen;
	} else {
	  length = strlen(foo);
	}
	h = rec[t].db_flen - length;
	strncpy((char *)&data[idx+h], foo, length);
      }
      //printf("ii6\n");printf("is %d\n",t);

    }
    //printf("iii5");
    idx += rec[t].db_flen;
  }
  //printf("iii4\n");
  if (write(dbh->db_fd, data, dbh->db_rlen) == -1) {
    return DBF_ERROR;
  }
  
  dbh->db_offset += dbh->db_rlen;
  
  return 0;
}

static int dbf_get_memo_data(dbhead *dbh, field *fldp,  u_long blknum)
{
    u_long	offset;
    u_long	req_offset;
    u_long	len;
    u_long	typ;
    u_char	*sp;
    int		done;
    int		i;

    req_offset = blknum * dbh->mb_blksiz;
    if ((offset = lseek(dbh->db_memofd, req_offset, SEEK_SET)) == -1)
    {
	perror("memofile lseek");
	return 1;
    }

    if (offset != req_offset)
    {
	char	msg[256];
	int	saverrno = errno;

	snprintf(msg, sizeof(msg),
		 "memofile lseek failed, block %lu expected %lu actual %lu code %d",
		 blknum,
		 req_offset,
		 offset,
		 saverrno);
	msg[sizeof(msg)-1] = 0;
	errno = saverrno;
	perror(msg);
	memset(fldp->db_contents, 0, fldp->db_blen);
	return 1;
    }

    switch(dbh->db_memo)
    {
      case DBF_MTYPE_FPT:
	if ((read(dbh->db_memofd, dbh->mb_buffer, sizeof(dbf_memo_block_header))) < 0)
	{
	    perror("memofile read");
	    return 1;
	}
	sp = ((dbf_memo_block_header *)(dbh->mb_buffer))->header;
	typ = (sp[0]<<24) + (sp[1]<<16) + (sp[2]<<8) + (sp[3]<<0);
	sp = ((dbf_memo_block_header *)(dbh->mb_buffer))->length;
	len = (sp[0]<<24) + (sp[1]<<16) + (sp[2]<<8) + (sp[3]<<0);
#if 0
	printf("memo-read blk: %3lu offset %5lu(0x%04lx) type %2lx size %3ld(0x%03lx)",
	       blknum, offset, offset, typ, len, len);
#endif
	if (dbh->mb_bufsiz < len)
	{
	    int	newsiz = (((len + 4095)/4096)+1)*4096-16;
	    void *tmpptr = realloc(dbh->mb_buffer, newsiz);
	    if (tmpptr != NULL)
	    {
		dbh->mb_buffer = tmpptr;
		dbh->mb_bufsiz = newsiz;
	    }
	}
	len = (len < dbh->mb_bufsiz) ? len : dbh->mb_bufsiz;
	if ((read(dbh->db_memofd, dbh->mb_buffer, len)) < 0)
	{
	    perror("memofile read");
	    return 1;
	}

	switch(typ)
	{
	  case 0x00:		/* Picture type */
	  case 0x02:		/* Object type */
	    fldp->db_contents[0] = '\0';
	    break;

	  case 0x01:		/* Memo type */
	    if (fldp->db_blen <= len)
	    {
		void *tmpptr = realloc(fldp->db_contents, len + 1);
		if (tmpptr != NULL)
		{
		    fldp->db_contents = tmpptr;
		    fldp->db_blen = len + 1;
		}
	    }
	    len = (len < fldp->db_blen) ? len : fldp->db_blen - 1;
	    strncpy(fldp->db_contents, (char *)dbh->mb_buffer, len);
	    fldp->db_contents[len] = '\0';
#if 0
	    printf(" '%s'\n", fldp->db_contents);
#endif
	    break;

	  default:	/* Something wrong */
	    break;
	}
	break;

      case DBF_MTYPE_DBT3:
	done = 0;
	len = 0;
	sp = dbh->mb_buffer;
	while(!done)
	{
	    if (dbh->mb_bufsiz < len + DBF_DBT_BLOCK_SIZE)
	    {
		int	newsiz = len + DBF_DBT_BLOCK_SIZE * 4;
		void	*tmpptr = realloc(dbh->mb_buffer, newsiz);
		if (tmpptr != NULL)
		{
		    dbh->mb_buffer = tmpptr;
		    dbh->mb_bufsiz = newsiz;
		    sp = dbh->mb_buffer + len;
		}
		else
		{
		    perror("dbIII memo field malloc failure");
		    exit(1);
		}
	    }
	    if (read(dbh->db_memofd, sp, DBF_DBT_BLOCK_SIZE) < DBF_DBT_BLOCK_SIZE)
	    {
		perror("memofile read");
		fldp->db_contents[0] = '\0';
		return 1;
	    }
	    for (i = 0; i < DBF_DBT_BLOCK_SIZE; i++)
	    {
		if (*sp != 0x1a)
		{
		    len++;
		    sp++;
		}
		else
		{
		    done = 1;
		    break;
		}
	    }
	}
	*sp = '\0';
	if (fldp->db_blen <= len)
	{
	    void *tmpptr = realloc(fldp->db_contents, len + 1);
	    if (tmpptr != NULL)
	    {
		fldp->db_contents = tmpptr;
		fldp->db_blen = len + 1;
	    }
	}
	len = (len < fldp->db_blen) ? len : fldp->db_blen - 1;
	strncpy(fldp->db_contents, (char *)dbh->mb_buffer, len);
	fldp->db_contents[len] = '\0';
	break;

      case DBF_MTYPE_DBT4:
	if ((read(dbh->db_memofd, dbh->mb_buffer, sizeof(dbf_memo_block_header))) < 0)
	{
	    perror("memofile read");
	    return 1;
	}
	sp = ((dbf_memo_block_header *)(dbh->mb_buffer))->header;
	typ = (sp[0]<<24) + (sp[1]<<16) + (sp[2]<<8) + (sp[3]<<0);
	len = get_long(((dbf_memo_block_header *)(dbh->mb_buffer))->length);
	if (typ != 0xFFFF0800)
	{
	    fprintf(stderr, "corrupt memo header in field %s memo block %lu\n",
		    fldp->db_name, blknum);
	    memset(fldp->db_contents, 0, fldp->db_blen);
	    return 1;
	}
	if (dbh->mb_bufsiz < len)
	{
	    int	newsiz = (((len + 4095)/4096)+1)*4096-16;
	    void *tmpptr = realloc(dbh->mb_buffer, newsiz);
	    if (tmpptr != NULL)
	    {
		dbh->mb_buffer = tmpptr;
		dbh->mb_bufsiz = newsiz;
	    }
	}
	len = (len < dbh->mb_bufsiz) ? len : dbh->mb_bufsiz;
	if ((read(dbh->db_memofd, dbh->mb_buffer, len)) < 0)
	{
	    perror("memofile read");
	    return 1;
	}
	if (fldp->db_blen <= len)
	{
	    void *tmpptr = realloc(fldp->db_contents, len + 1);
	    if (tmpptr != NULL)
	    {
		fldp->db_contents = tmpptr;
		fldp->db_blen = len + 1;
	    }
	}
	len = (len < fldp->db_blen) ? len : fldp->db_blen - 1;
	strncpy(fldp->db_contents, (char *)dbh->mb_buffer, len);
	fldp->db_contents[len] = '\0';
	break;

      case DBF_MTYPE_NONE:
	fldp->db_contents[0] = '\0';
	break;

      default:
	fprintf(stderr, "Sorry don't support memo fields on this database type yet\n");
	dbh->db_memo = DBF_MTYPE_NONE;
	return 1;
    }
    return 0;
}
