/*
 *  bin2c
 *
 *  Copyright (C) 2019  Skip Hansen
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>


// bin2ram_init <binary file> <output file>

void Usage(void);

int main(int argc, char **argv)
{
   FILE *fin = NULL;
   FILE *fout = NULL;
   int Ret = 0;
   uint8_t ReadBuffer[32];
   int BytesRead;
   int i;
   int InitBlock = 0;

   do {
      if(argc != 3) {
         Usage();
         break;
      }
      if((fin = fopen(argv[1],"r")) == NULL) {
         printf("Error: couldn't open %s - %s\n",argv[1],strerror(errno));
         Ret = errno;
         break;
      }

      if((fout = fopen(argv[2],"w")) == NULL) {
         printf("Error: couldn't open %s - %s\n",argv[2],strerror(errno));
         Ret = errno;
         break;
      }

      for( ; ; ) {
         memset(ReadBuffer,0,sizeof(ReadBuffer));

         if((BytesRead = fread(ReadBuffer,1,sizeof(ReadBuffer),fin)) <= 0) {
            if(BytesRead == 0) {
               break;
            }
            else {
               printf("Read error - %s\n",strerror(errno));
               Ret = errno;
            }
            break;
         }
         if(InitBlock != 0) {
            if(fprintf(fout,",\n") < 0) {
               printf("Write error - %s\n",strerror(errno));
               Ret = errno;
               break;
            }
         }

         if(fprintf(fout,".INIT_%02x(256'h",InitBlock++) < 0) {
            printf("Write error - %s\n",strerror(errno));
            Ret = errno;
            break;
         }

         for(i = sizeof(ReadBuffer) - 1; i >= 0; i--) {
            if(fprintf(fout,"%02x",ReadBuffer[i]) < 0) {
               printf("Write error - %s\n",strerror(errno));
               Ret = errno;
               break;
            }
         }
         if(fprintf(fout,")") < 0) {
            printf("Write error - %s\n",strerror(errno));
            Ret = errno;
            break;
         }
      }

      if(fprintf(fout,"\n") < 0) {
         printf("Write error - %s\n",strerror(errno));
         Ret = errno;
         break;
      }
   } while(0);

   if(fin != NULL) {
      fclose(fin);
   }

   if(fout != NULL) {
      fclose(fout);
   }

   return Ret;
}

void Usage()
{
   printf("bin2ram_init <binary file> <output file>\n");
}



