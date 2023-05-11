#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include "ext2_fs.h"

ssize_t pread(int fd, void *buf, size_t count, off_t offset);
void print_dir_entries(int disk_image, int block_size, struct ext2_inode* inode, int inode_number);
void print_inode(struct ext2_inode inode, int inode_number);



int main(int argc, char** argv) {

    if(argc!=2){
        printf("INCORRECT # OF ARGUMENTS");
    }

    int file_system;
    file_system = open(argv[1], O_RDONLY);
    if (file_system < 0){
        perror("Failed to Open File");
        exit(1);
    }

    /* STAGE 1: SUPERBLOCK INFO */

    //loads all of the superblock data that is located 1024 bytes offset from the beginning of the disk image file. 
    struct ext2_super_block superblock;
    pread(file_system, &superblock, sizeof(superblock), 1024);

    //variables for superblock section
    int num_blocks = superblock.s_blocks_count;
    int num_inodes = superblock.s_inodes_count;
    int block_size = 1024 << superblock.s_log_block_size;
    int inode_size = superblock.s_inode_size;
    int blocks_per_group = superblock.s_blocks_per_group;
    int inodes_per_group = superblock.s_inodes_per_group;
    int first_non_reserved_inode = EXT2_GOOD_OLD_FIRST_INO;

    //first line
    printf("SUPERBLOCK,%d,%d,%d,%d,%d,%d,%d\n", num_blocks, num_inodes, block_size, inode_size, blocks_per_group, inodes_per_group, first_non_reserved_inode);

    /* END OF STAGE 1, SUPERBLOCK*/


    int group_size = sizeof(struct ext2_group_desc); //save group size for later
    struct ext2_group_desc group_desc; // This variable is re-used a lot

    for(int i = 0; i < (int)(superblock.s_blocks_count/superblock.s_blocks_per_group + 1); i++){
        

        ssize_t n = pread(file_system, &group_desc, group_size, (superblock.s_first_data_block + 1) * block_size + i * group_size);
        if(n != group_size){
            perror("Size Incorrect");
            exit(1);
        }

        /*STAGE 2, GROUP INFO: */

        //get id of last full group: 
        int id_last_full_group = superblock.s_blocks_count/superblock.s_blocks_per_group;
        int num_blocks;


        // All of this is to handle the case where a block is volume maxed so it can't have as many blocks as blocks_per_group. 
        if(i < id_last_full_group){
            num_blocks = superblock.s_blocks_per_group;
        }
        else{
            num_blocks = superblock.s_blocks_count % superblock.s_blocks_per_group;
            // the size is exactly large enough to fit the num_blocks_per_group for every group
            if(num_blocks == 0){
                num_blocks = superblock.s_blocks_per_group;
            }
        }

        int inodes_per_group = superblock.s_inodes_per_group;
        int free_blocks = group_desc.bg_free_blocks_count;
        int free_inodes = group_desc.bg_free_inodes_count;
        int block_num_free_block_bitmap = group_desc.bg_block_bitmap;
        int block_num_free_inode_bitmap = group_desc.bg_inode_bitmap;
        int block_num_first_block_inodes = group_desc.bg_inode_table; 

        printf("GROUP,%d,%d,%d,%d,%d,%d,%d,%d\n",
            i, num_blocks, inodes_per_group, free_blocks, free_inodes, block_num_free_block_bitmap, block_num_free_inode_bitmap, block_num_first_block_inodes);

        /* END OF STAGE 2, GROUP INFO*/

        /*STAGE 3, BFREE*/

        uint8_t *block_bitmap = malloc(block_size);
        n = pread(file_system, block_bitmap, block_size, group_desc.bg_block_bitmap * block_size);
        if(n != block_size){
            perror("Size Incorrect STAGE 3");
            exit(1);
        }

        for (int j = 0; j < (int)(superblock.s_blocks_per_group); j++){
            int bit_offset = j%8;
            int byte_offset = j/8;
            uint8_t bit_mask = 1 << bit_offset;
            if((block_bitmap[byte_offset] & bit_mask) == 0){
                int block_num = i * superblock.s_blocks_per_group + j;
                printf("BFREE,%d\n", block_num+1);
                // we continue so we do not print info about this Inode in later stages
                continue;
            }
        }
        
        free(block_bitmap);

        /* END OF STAGE 3, BFREE */

        /* STAGE 4, IFREE */
        uint8_t *inode_bitmap = malloc(block_size);
        n = pread(file_system, inode_bitmap, block_size, group_desc.bg_inode_bitmap * block_size);
        if(n != block_size){
            perror("Size Incorrect STAGE 4, bitmap");
            exit(1);
        }

        for (int j = 0; j < (int)(superblock.s_inodes_per_group); j++){
            int bit_offset = j%8;
            int byte_offset = j/8;
            uint8_t bit_mask = 1 << bit_offset;
            if((inode_bitmap[byte_offset] & bit_mask) == 0){
                printf("IFREE,%d\n", j+1);
            } 

        }

        free(inode_bitmap);

        /* END OF STAGE 4, IFREE */

        //loop over every inode in the inode table, this handles all stages past this point
        for(int k = 0; k < (int)(superblock.s_inodes_per_group); k++){
            
            
            /* BEGINNING OF STAGE 5, INODE SUMMARY*/

            struct ext2_inode inode;
            inode_size = sizeof(inode);
            n = pread(file_system, &inode, inode_size, group_desc.bg_inode_table * block_size + k * inode_size);
            if(n != inode_size){
                perror("Size Incorrect STAGE 5, inode size incorrect");
                exit(1);
            }

            //helper function that prints all information related to just the inode, no directory or indirect stuff here
            print_inode(inode, k);

            /* END OF STAGE 5, INODE SUMMARY*/

            /* BEGINNING OF STAGE 6, DIRECTORY ENTRY'S*/

            //check if inode is a directory node
            if(S_ISDIR(inode.i_mode)) {
                print_dir_entries(file_system, block_size, &inode, k);
            }




        }
    //frees the array integers tracking inode status for this group_desc    
    }

    close(file_system);
    return 0;

}


void print_inode(struct ext2_inode inode, int inode_number) {

    //verify this inode is not free
    if((inode.i_mode==0 && inode.i_links_count==0)) {
        return;
    }

    // code to determine filetype of used inode. 
    char file_type;
    if(S_ISREG(inode.i_mode)){
        file_type = 'f';
    }
    else if(S_ISDIR(inode.i_mode)){
        file_type = 'd';
    }
    else if(S_ISLNK(inode.i_mode)){
        file_type = 's';
    }
    else{
        file_type = '?';
    }

    inode_number = inode_number;
    char inode_file_type = file_type; 
    mode_t mode = inode.i_mode & 0xFFF;
    __u16 owner = inode.i_uid;
    __u16 group = inode.i_gid;
    __u16 link_count = inode.i_links_count;
    __u32 file_size = inode.i_size;
    __u32 block_count = inode.i_blocks;

    //time data transformations, same thing for all 3:
    __u32 time1 = inode.i_ctime;
    __u32 time2 = inode.i_mtime;
    __u32 time3 = inode.i_atime;

    time_t creation_time = (time_t)(time1);
    struct tm *inode_ctime = gmtime(&creation_time);
    char creation_time_final[20];
    strftime(creation_time_final, 20, "%m/%d/%y %H:%M:%S", inode_ctime);

    time_t modification_time = (time_t)(time2);
    struct tm *inode_mtime = gmtime(&modification_time);
    char modification_time_final[20];
    strftime(modification_time_final, 20, "%m/%d/%y %H:%M:%S", inode_mtime);

    time_t access_time = (time_t)(time3);
    struct tm *inode_atime = gmtime(&access_time);
    char access_time_final[20];
    strftime(access_time_final, 20, "%m/%d/%y %H:%M:%S", inode_atime);

    // end of time date transformations

    //the colossal print:
    printf("INODE,%d,%c,%o,%d,%d,%d,%s,%s,%s,%d,%d"
    ,inode_number+1, inode_file_type, mode, owner, group, link_count
    ,creation_time_final, modification_time_final
    ,access_time_final, file_size, block_count);

    //prints for edge cases depending on Inode type:
    if(S_ISREG(inode.i_mode) || S_ISDIR(inode.i_mode)) {
        for(int j = 0; j < 15; j++){
            printf(",%d", inode.i_block[j]);
        }
    } else if(S_ISLNK(inode.i_mode) && inode.i_size<60) {
        printf(",%d", inode.i_block[0]);
    } else if(S_ISLNK(inode.i_mode)) {
        for(int j = 0; j < 15; j++) {
            if(inode.i_block[j]!=0) {
                printf(",%d", inode.i_block[j]);
            }
        }
    }

    printf("\n");

}


void print_dir_entries(int disk_image, int block_size, struct ext2_inode* inode, int inode_number){

    //initialize a directory entry struct and a name field for it
    struct ext2_dir_entry dir_entry;
    int dir_entry_size = sizeof(dir_entry);
    int cumulative_offset = 0;
    //loop over all block entries for the give inode
    for(int i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        //loop over all the directory entries in the data block, but skip the iteration if the block is 0
        if(inode->i_block[i] == 0) {
            continue;
        }
        off_t global_offset = inode->i_block[i] * block_size;
        off_t dir_offset = 0;
        while (dir_offset < block_size) {

            //load the directory entry
            pread(disk_image, &dir_entry, dir_entry_size, global_offset + dir_offset);

            //for readability and convenience I duplicate these
            inode_number = inode_number;
            cumulative_offset = cumulative_offset;
            __u32 inode_referenced = dir_entry.inode;
            __u16 entry_length = dir_entry.rec_len;
            __u8 name_length = dir_entry.name_len;

            //code to ensure filename is copies safely and is the correct length
            char name[256];
            memcpy(name, dir_entry.name, name_length);
            name[name_length] = '\0';

            //break before printing an invalid entry, increment j otherwise
            if(entry_length == 0 || name_length==0) {
                break;
            }

            //print directory entry information for this entry
            printf("DIRENT,%d,%d,%d,%d,%d,'%s'\n", inode_number+1, cumulative_offset, inode_referenced
            ,entry_length, name_length, name);


            //increment J by the length of this entry
            cumulative_offset+=entry_length;
            dir_offset+=entry_length;
            
        }

    }


}