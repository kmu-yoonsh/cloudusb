//
//  fat_custom.c
//  CloudUSB
//
//  Created by Hanul on 01/05/2017.
//  Copyright © 2017 Hanul. All rights reserved.
//

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include "fat_filelib.h"
#include "fat_types.h"
#include "fat_custom.h"

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
extern struct fatfs _fs;

struct direntry *_direntries[DIR_ENTRY_TABLE_FULL];
struct dataentry *_dataentries[DATA_ENTRY_TABLE_FULL];
struct module_init inits;

int module_fd;
uint8 module_buffer[BUFF_LEN_FULL];

//-----------------------------------------------------------------------------
// Locals
//-----------------------------------------------------------------------------
static char _script_path[PATH_LEN_FULL];
static char _pipe_path[PATH_LEN_FULL];

static unsigned char _br[FAT_SECTOR_SIZE];
static unsigned char _reserved_area_first[FAT_SECTOR_SIZE];
static unsigned char _fat_area[FAT_AREA_FULL]; // Need to dynamic allocation...

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------
void read_path(char *exec_path)
{
    strcpy(_script_path, exec_path);
    strcpy(_pipe_path, exec_path);
    
    strcpy(&(_script_path[strlen(_script_path)-8]), "../src/googledrive/list/list.py");
    strcpy(&(_pipe_path[strlen(_pipe_path)-8]), "myfifo");
}

void create_rootdir_entry()
{
    create_direntry(_fs.rootdir_first_cluster);
}

int read_virtual(uint32 sector, uint8 *buffer, uint32 sector_count)
{
    int sector_loc = sector;
    int read_count = 0;
    
    while (sector_count > read_count && read_count < 32)
    {
        // (Boot record)
        // 6 -> Do not know why...
        if (sector_loc == 0 || sector_loc == 6)
        {
            memcpy(buffer+read_count*FAT_SECTOR_SIZE, _br, FAT_SECTOR_SIZE);
        }
        
        // (Reserved area)
        // 7 -> Do not know why too...
        else if (sector_loc == 1 || sector_loc == 7)
        {
            memcpy(buffer+read_count*FAT_SECTOR_SIZE, _reserved_area_first, FAT_SECTOR_SIZE);
        }
        
        // (FAT area)
        else if ((_fs.fat_begin_lba <= sector_loc && sector_loc < _fs.fat_begin_lba +_fs.fat_sectors) ||
                 (_fs.fat_begin_lba + _fs.fat_sectors <= sector_loc && sector_loc < _fs.fat_begin_lba + 2*_fs.fat_sectors))
        {
            unsigned long loc = (sector_loc - _fs.fat_begin_lba) * FAT_SECTOR_SIZE;
            
            memcpy(buffer+read_count*FAT_SECTOR_SIZE, _fat_area + loc, FAT_SECTOR_SIZE);
        }
        
        // (Directory entry & Data entry)
        else if (sector_loc >= _fs.rootdir_first_sector)
        {
            unsigned long i;
            unsigned long cluster = (sector_loc - _fs.rootdir_first_sector)/_fs.sectors_per_cluster + _fs.rootdir_first_cluster;
            
            // Search directory entry cluster table
            for (i=0; i<DIR_ENTRY_TABLE_FULL; ++i)
            {
                if(_direntries[i] == NULL)
                    break;
                
                if(_direntries[i]->cluster == cluster)
                {
                    // Location in cluster
                    // Do not touch it it's my masterpiece
                    unsigned long loc = ((sector_loc - _fs.rootdir_first_sector) % _fs.sectors_per_cluster) * FAT_SECTOR_SIZE;
                    
                    memcpy(buffer, _direntries[i]->entry + loc, (sector_count-read_count) * FAT_SECTOR_SIZE);
                    
                    return 1;
                }
            }
            
            // Search data cluster table
            for(i=0; i<DATA_ENTRY_TABLE_FULL; ++i)
            {
                if(_dataentries[i] == NULL)
                    break;
                
                uint32 size = _dataentries[i]->size;
                uint32 endcluster = _dataentries[i]->startcluster + size/FAT_CLUSTER_SIZE + ((size%FAT_CLUSTER_SIZE)?1:0);
                
                // The file requested
                if(_dataentries[i]->startcluster <= cluster && cluster < endcluster)
                {
                    // Download from Google drive & Open & Read
                    unsigned long loc;
                    loc = (sector_loc - _fs.rootdir_first_sector);
                    loc -= (_dataentries[i]->startcluster - _fs.rootdir_first_cluster) * _fs.sectors_per_cluster;
                    
                    // Is downloaded?
                    if(!_dataentries[i]->download)
                    {
                        printf("[File Download] FID = %s\n", _dataentries[i]->id);
                        
                        _dataentries[i]->fd = download_file(_dataentries[i]->id);
                        _dataentries[i]->download = TRUE;
                    }
                    
                    read_file(_dataentries[i]->fd, loc, buffer+read_count*FAT_SECTOR_SIZE, (sector_count-read_count));
                    
                    return 1;
                }
            }
            
            memset(buffer+read_count*FAT_SECTOR_SIZE, 0x00, FAT_SECTOR_SIZE);
        }
        
        // (Meaning less location)
        else
        {
            memset(buffer+read_count*FAT_SECTOR_SIZE, 0x00, FAT_SECTOR_SIZE);
        }
        
        sector_loc ++;
        read_count ++;
    }
    
    return 1;
}

int write_virtual(uint32 sector, uint8 *buffer, uint32 sector_count)
{
    if(!sector || !buffer || !sector_count)
        return -1;
    
    // (Reserved area)
    // 7 -> Do not know why too...
    if(sector == 1 || sector == 7)
    {
        memcpy(_reserved_area_first, buffer, FAT_SECTOR_SIZE);
    }
    
    // (FAT area)
    else if(_fs.fat_begin_lba <= sector && sector < _fs.rootdir_first_sector)
    {
        unsigned long loc = (sector - _fs.fat_begin_lba) % _fs.fat_sectors * FAT_SECTOR_SIZE;
        
        memcpy(_fat_area + loc, buffer, sector_count * FAT_SECTOR_SIZE);
    }
    
    // (Directory entry)
    else
    {
        unsigned long i;
        
        // Location in cluster
        unsigned long loc = (sector - _fs.rootdir_first_sector) % _fs.sectors_per_cluster * FAT_SECTOR_SIZE;
        unsigned long cluster = (sector - _fs.rootdir_first_sector) / _fs.sectors_per_cluster + _fs.rootdir_first_cluster;
        
        // Search directory entry table
        for (i=0; i<DIR_ENTRY_TABLE_FULL; ++i)
        {
            if(_direntries[i] == NULL)
                break;
            
            if(_direntries[i]->cluster == cluster)
            {
                memcpy(_direntries[i]->entry + loc, buffer, sector_count * FAT_SECTOR_SIZE);
                
                return 1;
            }
        }
        
        // Search data table
        // Incompletion...
        for(i=0; i<DATA_ENTRY_TABLE_FULL; ++i)
        {
            if(_dataentries[i] == NULL)
                return 0;
            
            if(_dataentries[i]->startcluster <= cluster &&
               cluster < (_dataentries[i]->startcluster + (_dataentries[i]->size/FAT_CLUSTER_SIZE)))
                return 1;
        }
        
        return -1;
    }
    
    return 1;
}

void download_metadata()
{
    char cmd[CMD_LEN_FULL] = "python ";
    strcat(cmd, _script_path);
    strcat(cmd, " --path ");
    strcat(cmd, _pipe_path);
    
    system(cmd);
}

// input : <filename, filesize, fileid, isdir>
void read_pipe(char *buffer)
{
    int fd;
    
    fd = open(_pipe_path, O_RDONLY);
    read(fd, buffer, PIPE_LEN_FULL);
    
    printf("Recieved : [\n%s\n]\n", buffer);
    
    close(fd);
}

int create_direntry(uint32 startcluster)
{
    int i;
    
    for(i=0; i<DIR_ENTRY_TABLE_FULL; ++i)
    {
        if(_direntries[i] == NULL)
        {
            _direntries[i] = (struct direntry*) malloc(sizeof(struct direntry));
            
            memset(_direntries[i], 0x00, sizeof(unsigned char)*FAT_SECTOR_SIZE*_fs.sectors_per_cluster);
            
            _direntries[i]->cluster = startcluster;
            
            return 1;
        }
    }
    
    puts("[Error] Not Enough Dir Entry...");
    return 0;
}

int create_dataentry(uint32 startcluster, uint32 fsize, char *fid)
{
    int i;
    
    for(i=0; i<DATA_ENTRY_TABLE_FULL; ++i)
    {
        if(_dataentries[i] == NULL)
        {
            _dataentries[i] = (struct dataentry*)malloc(sizeof(struct dataentry));
            
            _dataentries[i]->startcluster = startcluster;
            _dataentries[i]->size = fsize;
            _dataentries[i]->download = FALSE;
            
            memcpy(_dataentries[i]->id, fid, FILE_ID_LEN_FULL);
            
            return 1;
        }
    }
    
    puts("[Error] Not Enough Data Entry...");
    return 0;
}

// Need to fix?
// Maximum filename is 260 bytes include path
void write_entries()
{
    char ch = -1;
    int offset = 0;
    int dir;
    uint32 fsize;
    char filelist[PIPE_LEN_FULL];
    char filename[FATFS_MAX_LONG_FILENAME];
    char fid[FILE_ID_LEN_FULL];
    
    
    read_pipe(filelist);
    
    while(ch != '\0')
    {
        sscanf(filelist+offset, "%260s %u %s %d", filename, &fsize, fid, &dir);
        
        write_entry(filename, fsize, fid, dir);
        
        while((ch = *(filelist+(offset++))) != '\n' && ch != '\0');
    }
}

// Download from google drive by file ID
// Return file descriptor
int download_file(char *fid)
{
    char cmd[CMD_LEN_FULL] = "python ";
    strncat(cmd, _script_path, strlen(_script_path)-12);
    strcat(cmd, "download/download.py --fid ");
    strcat(cmd, fid);
    
    system(cmd);
    
    return open(fid, O_RDONLY);
}

int read_file(int fd, unsigned long sector, unsigned char *buffer, unsigned long sector_count)
{
    lseek(fd, FAT_SECTOR_SIZE * sector, SEEK_SET);
    
    if(read(fd, buffer, FAT_SECTOR_SIZE * sector_count) < 0)
    {
        perror("read");
        return 0;
    }
    
    return 1;
}

void read_requested(uint32 offset, unsigned char *buffer, uint32 offset_count)
{
    read_virtual(offset/FAT_SECTOR_SIZE, buffer, offset_count/FAT_SECTOR_SIZE);
}

void run_module()
{
    signal(SIGUSR1, file_transfer);
    
    /* send to kernel module */
    inits.pid = getpid();
    printf("User Pid is %d\n", inits.pid);
    
    if((module_fd = open("/dev/CloudUSB", O_RDWR)) < 0)
    {
        puts("Device Open failed!!");
        printf("%d\n", errno);
        
        return ;
    }
    
    printf("STRUCT ADDRESS : %p\n", &inits);
    
    if(ioctl(module_fd, INIT, &inits) < 0)
        printf("Error in IOCTL1 errno: %d\n", errno);
    
    while(1)
        pause();
}

void file_transfer(int signo)
{
    struct return_file files;
    
    uint32 offset_count = inits.amount; // Block request length
    uint32 offset = inits.file_offset; // Block request start point
    
    memset(module_buffer, 0x00, BUFF_LEN_FULL);
    read_requested(offset, module_buffer, offset_count);
    
    files.buf = module_buffer; // substitute buffer address which have file info
    files.nread = inits.amount; // substitute buffer length which have file info
    
    if(ioctl(module_fd, RETURN_FILE, &files) < 0)
        printf("Error in IOCTL2 errno: %d\n", errno);
}

void create_reserved_area()
{
    // ~ 512 Byte
    unsigned char br[] =
    {
        0xEB, 0x58, 0x90, 0x6D, 0x6B, 0x66, 0x73, 0x2E, 0x66, 0x61, 0x74, 0x00, 0x02, 0x08, 0x20, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x20, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x20, 0x00, 0xFC, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x29, 0xB7, 0xF9, 0xD7, 0xA7, 0x4E, 0x4F, 0x20, 0x4E, 0x41, 0x4D, 0x45, 0x20, 0x20,
        0x20, 0x20, 0x46, 0x41, 0x54, 0x33, 0x32, 0x20, 0x20, 0x20, 0x0E, 0x1F, 0xBE, 0x77, 0x7C, 0xAC,
        0x22, 0xC0, 0x74, 0x0B, 0x56, 0xB4, 0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10, 0x5E, 0xEB, 0xF0, 0x32,
        0xE4, 0xCD, 0x16, 0xCD, 0x19, 0xEB, 0xFE, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x6E,
        0x6F, 0x74, 0x20, 0x61, 0x20, 0x62, 0x6F, 0x6F, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x20, 0x64, 0x69,
        0x73, 0x6B, 0x2E, 0x20, 0x20, 0x50, 0x6C, 0x65, 0x61, 0x73, 0x65, 0x20, 0x69, 0x6E, 0x73, 0x65,
        0x72, 0x74, 0x20, 0x61, 0x20, 0x62, 0x6F, 0x6F, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x20, 0x66, 0x6C,
        0x6F, 0x70, 0x70, 0x79, 0x20, 0x61, 0x6E, 0x64, 0x0D, 0x0A, 0x70, 0x72, 0x65, 0x73, 0x73, 0x20,
        0x61, 0x6E, 0x79, 0x20, 0x6B, 0x65, 0x79, 0x20, 0x74, 0x6F, 0x20, 0x74, 0x72, 0x79, 0x20, 0x61,
        0x67, 0x61, 0x69, 0x6E, 0x20, 0x2E, 0x2E, 0x2E, 0x20, 0x0D, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xAA
    };
    
    // 512 ~ 1024 Byte
    unsigned char reserved[] =
    {
        0x52, 0x52, 0x61, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x72, 0x72, 0x41, 0x61, 0xFC, 0xFD, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xAA
    };
    
    memcpy(_br, br, FAT_SECTOR_SIZE);
    memcpy(_reserved_area_first, reserved, FAT_SECTOR_SIZE);
}

void create_fat_area()
{
    memset(_fat_area, 0x00, sizeof(char)*FAT_AREA_FULL);
    
    unsigned char fat[] =
    {
        0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xF8, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00
    };
    
    memcpy(_fat_area, fat, sizeof(unsigned char)*16);
}
