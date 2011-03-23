/*	utility to read out an mySQL-table, and store it into a DBF-file

	M. Boekhold (boekhold@cindy.et.tudelft.nl) April 1996
*/

#define USE_OLD_FUNCTIONS

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <mysql.h>
#include "dbf.h"

int		verbose = 0, upper = 0, lower = 0, create = 0;
long	precision = 6;
char	*host = NULL;
char	*dbase = NULL;
char	*table = NULL;
char    *pass = NULL;
char    *user = NULL;

inline void strtoupper(char *string);
inline void strtolower(char *string);
void usage(void);

inline void strtoupper(char *string) {
        while(*string != '\0') {
                *string = toupper(*string);
		string++;
        }
}

inline void strtolower(char *string) {
        while(*string != '\0') {
                *string = tolower(*string);
		string++;
        }
}

void usage(void) {
	printf("mysql2dbf %s\n", VERSION);
	printf("usage:\tmysql2dbf [-h host] [-u | -l] [-v[v]]\n");
	printf("\t\t\t[-q query] [-P password] [-U user] -d dbase -t table dbf-file\n");
}

int main(int argc, char **argv) {
	int             i;
	MYSQL		*SQLsock,mysql;
	extern int      optind;
	extern char     *optarg;
	char            *query = NULL;

	dbhead          *dbh;
	field			*rec;
	MYSQL_RES		*qres;
	MYSQL_ROW			qrow;
	MYSQL_FIELD			*qfield;
	u_long			numfields, numrows, t;

	while ((i = getopt(argc, argv, "lucvq:h:d:t:p:U:P:")) != EOF) {
		switch(i) {
			case 'q':
				query = strdup(optarg);
				break;
			case 'v':
				verbose++;
				break;
			case 'c':
				create = 1;
				break;
			case 'l':
				if (upper) {
					usage();
					printf("Can't use -u and -l at the same time!\n");
					exit(1);
				}
				lower = 1;
				break;
			case 'u':
				if (lower) {
					usage();
					printf("Can't use -u and -l at the same time!\n");
					exit(1);
				}
				upper = 1;
				break;
			case 'h':
				host = (char *)strdup(optarg);
				break;
			case 'd':
				dbase = (char *)strdup(optarg);
				break;
			case 't':
				table = (char *)strdup(optarg);
				break;
		        case 'P':
				pass = (char *)strdup(optarg);
				break;
		        case 'U':
			        user = (char *)strdup(optarg);
				break;
			case ':':
				usage();
				printf("Missing argument!\n");
				exit(1);
			case '?':
				usage();
				printf("Unknown argument: %s\n", argv[0]);
				exit(1);
			default:
				break;
		}
	}

	argc -= optind;
	argv = &argv[optind];

	if (argc != 1) {
		usage();
		exit(1);
	}

	if ((dbase == NULL) || ((table == NULL) && (query == NULL))) {
		usage();
		printf("Dbase and table *must* be specified!\n");
		exit(1);
	}

	if (query == NULL) {
		query = (char *)malloc(14+strlen(table));
		sprintf(query, "SELECT * FROM %s", table);
	}

	if (verbose > 1) {
		printf("Opening %s for writing (previous contents will be lost)\n",
																	argv[0]);
	}

	if ((dbh = dbf_open_new(argv[0], O_WRONLY | O_CREAT | O_TRUNC)) == (dbhead *)DBF_ERROR) {
		fprintf(stderr, "Couldn't open xbase-file for writing: %s\n", argv[0]);
		exit(1);
	}

	if (verbose > 1) {
		printf("Making connection with mySQL-server\n");
	}

        mysql_init(&mysql);
        
    if (!(SQLsock = mysql_real_connect(&mysql,host,user,pass, NULL, 0, NULL, 0))) {
        fprintf(stderr, "Couldn't get a connection with the ");
        fprintf(stderr, "designated host!\n");
        fprintf(stderr, "Detailed report: %s\n", mysql_error(&mysql));
        close(dbh->db_fd);
        free(dbh);
        exit(1);
    }

    if (verbose > 1) {
        printf("Selecting database\n");
    }

    if ((mysql_select_db(SQLsock, dbase)) == -1) {
        fprintf(stderr, "Couldn't select database %s.\n", dbase);
        fprintf(stderr, "Detailed report: %s\n", mysql_error(SQLsock));
        close(dbh->db_fd);
        free(dbh);
        mysql_close(SQLsock);
        exit(1);
    }

	if (verbose > 1) {
		printf("Sending query\n");
	}
	

	if (mysql_query(SQLsock, query) == -1) {
		fprintf(stderr, "Error sending query.\nDetailed report: %s\n",
				mysql_error(SQLsock));
		if (verbose > 1) {
			fprintf(stderr, "%s\n", query);
		}
		mysql_close(SQLsock);
		close(dbh->db_fd);
		free(dbh);
		exit(1);
	}

	qres            = mysql_store_result(SQLsock);
	numfields       = mysql_num_fields(qres);
	numrows			= mysql_num_rows(qres);

	while ((qfield = mysql_fetch_field(qres))) {
	  
	  strtoupper(qfield->name);
	  switch (qfield->type) {
	    /* assume INT's have a max. of 10 digits (4^32 has 10 digits) */
	  case FIELD_TYPE_CHAR:
	  case FIELD_TYPE_SHORT:
	  case FIELD_TYPE_LONG:
	  
	    dbf_add_field(dbh, qfield->name, 'N', 10, 0);
	    if (verbose > 1) {
	      printf("Adding field: %s, INT_TYPE, %d\n", qfield->name,
		     qfield->length);
	    
	    }
	    break;
				/* do we have to make the same assumption here? */
	  case FIELD_TYPE_DOUBLE:
	  case FIELD_TYPE_FLOAT:
	  case FIELD_TYPE_DECIMAL:
	    dbf_add_field(dbh, qfield->name, 'N', qfield->length,
			  qfield->decimals);
	    if (verbose > 1) {
	      printf("Adding field: %s, INT_REAL, %d\n", qfield->name,
		     qfield->length);
	    }
	    break;
	  case FIELD_TYPE_STRING:
	  case FIELD_TYPE_VAR_STRING:
	  case FIELD_TYPE_BLOB:
	  case FIELD_TYPE_TINY_BLOB:
	  case FIELD_TYPE_MEDIUM_BLOB:
	  case FIELD_TYPE_LONG_BLOB:
	    dbf_add_field(dbh, qfield->name, 'C', qfield->length, 0);
	    if (verbose > 1) {
	      printf("Adding field: %s, INT_CHAR, %d\n", qfield->name,
		     qfield->length);
	    }
	    break;
	  case FIELD_TYPE_DATETIME:
	  case FIELD_TYPE_TIMESTAMP:
	    dbf_add_field(dbh, qfield->name, 'T', qfield->length, 0);
	    if (verbose > 1) {
	     printf("Adding field: %s, FIELD_TYPE_DATETIME, %d\n", qfield->name,
	        qfield->length);
	    }
	    break;
	  case FIELD_TYPE_DATE:
	  case FIELD_TYPE_NEWDATE:
	    //dbf_add_field(dbh, qfield->name, 'D', qfield->length, 0);
	    dbf_add_field(dbh, qfield->name, 'D', 8, 0); // date is 8 in dbf, Mysql returns 10 ?!
	    if (verbose > 1) {
	      printf("Adding field: %s, FIELD_TYPE_DATE, %d\n", qfield->name,8);
	      //qfield->length);
	    }
	    break;
	  default:
	    break;
	  }
	}
	
	if (verbose > 1) {
	  printf("Writing out header\n");
	}
	
	dbf_write_head(dbh);
	if (verbose > 1) {
	  printf("Writing out field-descriptions\n");
	}
	dbf_put_fields(dbh);
	if (verbose > 1) {
	  printf("Writing out data\n");
	}
	if (numrows) {
	  while ((qrow = mysql_fetch_row(qres)) != NULL) {

	    if ((rec = dbf_build_record(dbh)) != (field *)DBF_ERROR) {
	       
	      for (t = 0; t < numfields; t++) {
		//printf("ROW: %s\n",qrow[t]);
		if (qrow[t] != 0) {
		  strcpy(rec[t].db_contents, qrow[t]);
		} else {
		  rec[t].db_contents[0] = '\0';
		}
		if (upper) {
		  strtoupper(rec[t].db_contents);
		}
		if (lower) {
		  strtolower(rec[t].db_contents);
		}
	      }
	 
	      dbf_put_record(dbh, rec, dbh->db_records + 1);
	    
	      dbf_free_record(dbh, rec);
	  
	    }
	  }
	}
	
	if (verbose > 1) {
		printf("Writing out header\n");
	}

	dbf_write_head(dbh);

	if (verbose > 1) {
		printf("Closing up\n");
	}
				
	mysql_free_result(qres);
	mysql_close(SQLsock);
	close(dbh->db_fd);
	free(dbh);
	exit(0);
}			
