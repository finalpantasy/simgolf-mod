"""Read ratings without loading a save into live game memory."""
from pathlib import Path
import unittest
import test_publication_allocations as allocations
from test_publication_allocations import function

ROOT=Path(__file__).resolve().parents[2]

class SaveRatings(unittest.TestCase):
    run_c=allocations.PublicationAllocations.run_c

    def test_production_sort_preserves_filename_identity_and_recent_ties(self):
        source=(ROOT/'port/src/port/win32.c').read_text()
        header=(ROOT/'port/src/port/save_ratings.h').read_text().replace('#pragma once','')
        entry=source[source.index('typedef struct { char name[260];'):source.index('} FindEntry;')+len('} FindEntry;')]
        bodies='\n'.join(function(source,x) for x in ['static unsigned find_fold(', 'static int find_recent(', 'static int find_rated('])
        self.run_c('save_sort','#define _CRT_SECURE_NO_WARNINGS\n#include <assert.h>\n#include <stdlib.h>\n'+header+entry+'\nstatic int g_save_sort_mode;\n'+bodies+r'''
int main(int argc,char **argv){
 (void)argc;(void)argv;FindEntry rows[4]={0};
 strcpy(rows[0].name,"older.sve");rows[0].modified=1;rows[0].rating=(SaveRatings){80,6000};rows[0].rating_valid=1;
 strcpy(rows[1].name,"newer.sve");rows[1].modified=2;rows[1].rating=(SaveRatings){80,8000};rows[1].rating_valid=1;
 strcpy(rows[2].name,"unknown.sve");rows[2].modified=9;
 strcpy(rows[3].name,"skill.sve");rows[3].modified=3;rows[3].rating=(SaveRatings){70,12000};rows[3].rating_valid=1;
 g_save_sort_mode=1;qsort(rows,4,sizeof *rows,find_rated);
 assert(!strcmp(rows[0].name,"newer.sve")&&!strcmp(rows[1].name,"older.sve")&&!strcmp(rows[3].name,"unknown.sve"));
 g_save_sort_mode=2;qsort(rows,4,sizeof *rows,find_rated);
 assert(!strcmp(rows[0].name,"skill.sve")&&rows[0].modified==3&&rows[0].rating.skill==12000);
 qsort(rows,4,sizeof *rows,find_recent);assert(!strcmp(rows[0].name,"unknown.sve")&&!strcmp(rows[3].name,"older.sve"));
 return 0;
}
''',0)

    def test_metadata_cache_invalidates_on_identity_size_or_timestamp(self):
        source=(ROOT/'port/src/port/win32.c').read_text()
        header=(ROOT/'port/src/port/save_ratings.h').read_text().replace('#pragma once','')
        entry=source[source.index('typedef struct { char name[260];'):source.index('} FindEntry;')+len('} FindEntry;')]
        self.run_c('save_rating_cache','#define _CRT_SECURE_NO_WARNINGS\n#include <assert.h>\n#include <stdio.h>\n'+header+entry+r'''
static FindEntry g_save_rating_cache[100];static int g_save_rating_count;
static FILE *fixture;static int resolved;
static int fs_resolve(const char *dos,char *out,int cap,int writing){assert(!writing&&cap>20);assert(!strncmp(dos,"Saved Games/",12));strcpy(out,"fixture");resolved++;return 1;}
static FILE *fixture_open(const char *p,const char *mode){assert(!strcmp(p,"fixture")&&!strcmp(mode,"rb"));rewind(fixture);return fixture;}
static int fixture_close(FILE *f){assert(f==fixture);return 0;}
#define fopen fixture_open
#define fclose fixture_close
''' + function(source,'int file_save_rating(')+function(source,'static void find_rating(')+r'''
#undef fopen
#undef fclose
int main(int argc,char **argv){
 (void)argc;(void)argv;fixture=tmpfile();assert(fixture);unsigned char prefix[137]={0};prefix[117]=73;assert(fwrite(prefix,1,sizeof prefix,fixture)==sizeof prefix);fflush(fixture);
 FindEntry e={0};strcpy(e.name,"A.sve");e.modified=10;e.size=137;find_rating(&e);assert(resolved==1&&e.rating_valid&&e.rating.fun==73);
 g_save_rating_cache[0]=e;g_save_rating_count=1;find_rating(&e);assert(resolved==1);
 e.modified++;find_rating(&e);assert(resolved==2);e.modified--;e.size++;find_rating(&e);assert(resolved==3);
 e.size--;strcpy(e.name,"B.sve");find_rating(&e);assert(resolved==4);
 e.modified=0;find_rating(&e);assert(resolved==5);
 SaveRatings out;assert(file_save_rating("A.sve",&out)&&out.fun==73);assert(!file_save_rating("B.sve",&out));
 fclose(fixture);return 0;
}
''',0)

    def test_legacy_extended_and_malformed_headers(self):
        source=(ROOT/'port/src/port/save_ratings.h').read_text().replace('#pragma once','')
        self.run_c('save_ratings','#define _CRT_SECURE_NO_WARNINGS\n#include <assert.h>\n'+source+r'''
static void put(unsigned char *p,uint32_t v){for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static void ratings(unsigned char *p){put(p+17,73);put(p+25,2500);put(p+29,4200);put(p+33,1800);}
int main(int argc,char **argv){
 (void)argc;(void)argv;unsigned char data[6000]={0};SaveRatings r;
 ratings(data+100);assert(save_ratings_parse(data,137,&r));assert(r.fun==73&&r.skill==8500);
 assert(!save_ratings_parse(data,136,&r));
 memset(data,0,sizeof data);memcpy(data+100,"SGMP",4);put(data+104,1);put(data+108,158);put(data+112,100);
 ratings(data+116);assert(save_ratings_parse(data,153,&r));assert(r.fun==73);
 put(data+104,2);assert(!save_ratings_parse(data,153,&r));put(data+104,1);
 put(data+108,10000);assert(!save_ratings_parse(data,153,&r));put(data+108,158);
 memcpy(data+116,"SGST",4);put(data+120,1);put(data+124,64);put(data+128,76);put(data+132,64*76);
 put(data+136,save_ratings_crc(data+140,64*76));ratings(data+140+64*76);
 assert(save_ratings_parse(data,140+64*76+37,&r));assert(r.skill==8500);
 data[150]^=1;assert(!save_ratings_parse(data,sizeof data,&r));data[150]^=1;
 put(data+132,0xffffffff);assert(!save_ratings_parse(data,sizeof data,&r));
 memset(data,0,sizeof data);ratings(data+100);put(data+125,0x7fffffff);put(data+129,0x7fffffff);put(data+133,0x7fffffff);
 assert(save_ratings_parse(data,137,&r));assert(r.skill==INT64_C(6442450941));
 SaveRatings a={70,9000},b={80,6000};
 assert(save_ratings_compare(&a,1,&b,1,1)>0);
 assert(save_ratings_compare(&a,1,&b,1,2)<0);
 assert(save_ratings_compare(&a,1,&b,0,1)<0);
 assert(save_ratings_compare(&a,0,&b,1,2)>0);
 assert(save_ratings_compare(&a,0,&b,0,2)==0);
 assert(save_ratings_compare(&a,1,&b,1,0)==0);
 assert(save_ratings_compare(&a,1,&a,1,2)==0);
 return 0;
}
''',0)
