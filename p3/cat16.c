#include "cat16.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define filename_bytes 8
#define extension_bytes 3
#define FOUR_MB 0x400000
#define ROOT_DIR_START_SECTOR 65 // 65*512
#define ROOT_DIR_END_SECTOR 97 // 97*512
#define SECTOR_SIZE 512
#define SECTORS_PER_CLUSTER 1
#define SECTORS_PER_FAT 32
#define TOTAL_SECTORS 0x2000
#define path_depth 99 // maximum depth of a path 
#define max_path_length 9999
#define ENTRY_SIZE 32

struct cat16_volume{
    long image_size;
    uint16_t sector_size; // 2 byte int
    uint8_t sectors_per_cluster; // 1 byte int
    uint16_t sectors_per_fat;
    uint8_t number_fats;
    uint16_t total_sectors;
    FILE *image_buffer;
    int last_error_code; 
};

struct cat16_dir_entry{
    unsigned char filename[filename_bytes]; 
    char extension[extension_bytes];
    uint8_t attributes;
    uint16_t reserved;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t hidden_bytes; // in the table there is a mysterious extra 2 bytes between these two fields
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_logical_cluster;
    uint32_t file_size;
    int byte_offset; // entries location in the image buffer
    int entry_found; // 1 if found 0 if not found
    bool deleted;
    char disk_name[filename_bytes + extension_bytes + 1]; // 8 + 3 + \0
    int start_sector; 
    int end_sector;
};

// my helper functions 

// move through the fat looking for a free cluster. returns the cluster number of the first free cluster found.
static uint16_t find_free_cluster(cat16_volume_t *volume){
    for (uint16_t cluster = 0x0002; cluster < 0xFFEF; cluster++){
        uint16_t value;
        fseek(volume->image_buffer, (SECTOR_SIZE * 1) + (cluster * sizeof(cluster)), SEEK_SET); // FAT1 STARTS AT SECTOR 1
        fread(&value, sizeof(value), 1, volume->image_buffer);
        if (value == 0x0000){
            return cluster;
        }
    }
    return 0xFFFF; // no free clusters found | out of space
}

//write a cluster number to the fat table at the specified cluster number.
static void write_fat_entry(cat16_volume_t *volume, uint16_t cluster, uint16_t value){
    fseek(volume->image_buffer, (SECTOR_SIZE * 1) + (cluster * sizeof(cluster)), SEEK_SET); // FAT1 STARTS AT SECTOR 1
    fwrite(&value, sizeof(value), 1, volume->image_buffer);
    fseek(volume->image_buffer, (SECTOR_SIZE * 33) + (cluster * sizeof(cluster)), SEEK_SET); // FAT2 STARTS AT SECTOR 33
    fwrite(&value, sizeof(value), 1, volume->image_buffer);
}

// search directory for a file or directory with the specified filename.
// returns a struct representing the directory entry if found, else empty entry with entry_found = 0 
static cat16_dir_entry_t search_directory(cat16_volume_t *volume, const char *filename, int start_sector, int end_sector){
    cat16_dir_entry_t entry;
    entry.entry_found = 0; // initialize entry_found to 0
    entry.start_sector = start_sector;
    entry.end_sector = end_sector;
    // search each availible sector for the dir entry
    for (int i = start_sector; i < end_sector; i++){
        // search each entry in the sector for the filename 512/32 16 entries per sector 
        for (int j  = 0; j < SECTOR_SIZE / ENTRY_SIZE; j++){
            // write the entry to the entry struct and see if it matches our filename
            fseek(volume->image_buffer, (i * SECTOR_SIZE) + (j * ENTRY_SIZE), SEEK_SET);
            fread(entry.filename, sizeof(entry.filename), 1, volume->image_buffer);
            fread(entry.extension, sizeof(entry.extension), 1, volume->image_buffer);
            fread(&entry.attributes, sizeof(entry.attributes), 1, volume->image_buffer);
            fread(&entry.reserved, sizeof(entry.reserved), 1, volume->image_buffer);
            fread(&entry.create_time, sizeof(entry.create_time), 1, volume->image_buffer);
            fread(&entry.create_date, sizeof(entry.create_date), 1, volume->image_buffer);
            fread(&entry.last_access_date, sizeof(entry.last_access_date), 1, volume->image_buffer);
            fread(&entry.last_write_time, sizeof(entry.last_write_time), 1, volume->image_buffer);
            fread(&entry.hidden_bytes, sizeof(entry.hidden_bytes), 1, volume->image_buffer);
            fread(&entry.last_write_date, sizeof(entry.last_write_date), 1, volume->image_buffer);
            fread(&entry.first_logical_cluster, sizeof(entry.first_logical_cluster), 1, volume->image_buffer);
            fread(&entry.file_size, sizeof(entry.file_size), 1, volume->image_buffer);
            entry.byte_offset = (i * SECTOR_SIZE) + (j * ENTRY_SIZE); // what byte the entry starts at
            // check if the entry is deleted. 
            if (entry.filename[0] == 0xE5){
                entry.deleted = true;
            }
            else{
                entry.deleted = false;
            }
            // Disk stores name with padding ex: A.txt -> 'A       .txt'
            // so transform our name to match
            char name_on_disk[13];
            int len = 0; // need external tracker since file name and entry are seperate
            for (int k = 0; k < 8; k++){
                if (entry.filename[k] != ' '){
                    name_on_disk[len] = entry.filename[k];
                    len++;
                }
            }
            // if an extesion exists add a '.' between name and extension
            if (entry.extension[0] != ' '){
                name_on_disk[len] = '.';
                len++;
                for (int k = 0; k < 3; k++){
                    if (entry.extension[k] != ' '){
                        name_on_disk[len] = entry.extension[k];
                        len++;
                    }
                }
            }
            name_on_disk[len] = '\0';
            strcpy(entry.disk_name, name_on_disk); // save name on disk to entry struct | in case I need it later
            // check if file name matches entries filename
            if (strcmp(name_on_disk, filename) == 0){
                entry.entry_found = 1;
                return entry;
            }
        }
    }
    return entry;
}




// walks through the provided path and searches for each file or directory in the path
// first search through root directory then just follow the first logical clusters. 
static int path_handler(cat16_volume_t *volume, const char *path, cat16_dir_entry_t *entry, int write_allowed){
    int current_sector = ROOT_DIR_START_SECTOR; 
    int end_sector = ROOT_DIR_END_SECTOR; 
    entry->entry_found = 0; // initialize entry_found to 0
    const char *path_ptr = path; // so I can move through the path without modifying the original string
    // skip leading slashs
    while (*path_ptr == '/'){
        path_ptr++;
    }
    // search each entry
    while (*path_ptr != '\0'){
        size_t len = strcspn(path_ptr, "/"); // len of string up to / 
        char entry_name[13]; // 8 + . + 3 + null terminator
        // filename too long
        if (len > sizeof(entry_name)){
            return -ENAMETOOLONG; 
        }
        strncpy(entry_name, path_ptr, len);
        entry_name[len] = '\0'; 
        path_ptr += len; 
        //skip slashes 
        while(*path_ptr == '/'){
            path_ptr++;
        }
        *entry = search_directory(volume, entry_name, current_sector, end_sector);
        // no dir/file found
        if (entry->entry_found == 0 && write_allowed == 0){
            return -ENOENT;
        }
        // end of path reached
        if (*path_ptr == '\0'){
            return 0; 
        }
        // update move to first logical cluster of a directory. 
        if (entry->attributes & 0x10) { 
            current_sector = 97 + (entry->first_logical_cluster - 2);
            end_sector = current_sector + 1;
        }
    }
    return -ENOENT; // empty path
}

static int write_dir_entry(cat16_volume_t *volume, cat16_dir_entry_t *entry, uint8_t attributes, uint16_t first_logical_cluster, uint32_t bytes_to_write){
    // 0 out unused fields
    entry->attributes = attributes; 
    entry->reserved = 0;
    entry->create_time = 0;
    entry->create_date = 0;
    entry->last_access_date = 0;
    entry->hidden_bytes = 0;
    entry->last_write_time = 0;
    entry->last_write_date = 0;
    entry->first_logical_cluster = first_logical_cluster;
    entry->file_size = bytes_to_write;
    // find a free entry in the root directory
    for (int i = entry->start_sector; i < entry->end_sector; i++){
        for (int j = 0; j < SECTOR_SIZE / ENTRY_SIZE; j++){
            fseek(volume->image_buffer, (i * SECTOR_SIZE) + (j * ENTRY_SIZE), SEEK_SET);
            uint8_t first_byte;
            fread(&first_byte, sizeof(first_byte), 1, volume->image_buffer);
            if (first_byte == 0x00 || first_byte == 0xE5){ // free entry found
                fseek(volume->image_buffer, (i * SECTOR_SIZE) + (j * ENTRY_SIZE), SEEK_SET);
                fwrite(entry->filename, sizeof(entry->filename), 1, volume->image_buffer);
                fwrite(entry->extension, sizeof(entry->extension), 1, volume->image_buffer);
                fwrite(&entry->attributes, sizeof(entry->attributes), 1, volume->image_buffer);
                fwrite(&entry->reserved, sizeof(entry->reserved), 1, volume->image_buffer);
                fwrite(&entry->create_time, sizeof(entry->create_time), 1, volume->image_buffer);
                fwrite(&entry->create_date, sizeof(entry->create_date), 1, volume->image_buffer);
                fwrite(&entry->last_access_date, sizeof(entry->last_access_date), 1, volume->image_buffer);
                fwrite(&entry->hidden_bytes, sizeof(entry->hidden_bytes), 1, volume->image_buffer);
                fwrite(&entry->last_write_time, sizeof(entry->last_write_time), 1, volume->image_buffer);
                fwrite(&entry->last_write_date, sizeof(entry->last_write_date), 1, volume->image_buffer);
                fwrite(&entry->first_logical_cluster, sizeof(entry->first_logical_cluster), 1, volume->image_buffer);
                fwrite(&entry->file_size, sizeof(entry->file_size), 1, volume->image_buffer);
                return 0; // success
            }
        }
    }
    return -ENOSPC; // no free entry found
}

// the error handling was easy to do. 
/*
 * cat16_vstrerror -- returns the error message corresponding
 *      to the last function failure. For example, if listdir
 *      just returned NULL, then this function would tell us
 *      why. Error codes should be stored on a per-volume
 *      basis. This function's behavior is undefined after
 *      a successful cat16 function return.
 * Return:
 *  - On success, it should return an error message
 *      corresponding to the last cat16_ function failure.
 *      You should use the standard error messages (returned
 *      by strerror() for the error codes specified in the
 *      project description. For any other error codes you
 *      choose to support, feel free to use an error message
 *      of your choosing.
 *  - On failure, it should return NULL. The only time it
 *      would do that is if volume is NULL.
 */
const char *cat16_vstrerror(cat16_volume_t *volume){
    if (volume == NULL){
        return NULL;
    }
    return cat16_strerror(volume->last_error_code);
}

/*
 * cat16_vstrerror -- returns the error message corresponding
 *      to the error_code, returned from a recent cat16_
 *      function failure. For example, if mkdir returned
 *      a negative error code, then this function would
 *      tell us what that code means. Note that since these
 *      messages are read only, they can be string constants.
 *      They do not need to be allocated on the heap. In
 *      fact, it's best if they are not, because I will not
 *      free them.
 * Return:
 *  - This function should always return an error message
 *      (including for success (0) and error_codes you don't
 *      recognize).
 *      If I pass in an error code you don't recognize, then
 *      you should still return a default message. You should
 *      use the standard error messages (returned by
 *      strerror() for the error codes specified in the
 *      project description. For any other error codes you
 *      choose to support, feel free to use an error message
 *      of your choosing.
 */
const char *cat16_strerror(int error_code){
    return strerror(-error_code);
}


/*
 * cat16_mount -- load's a FAT16 disk image into memory.
 * Return:
 *   - NULL: on failure if the image could not be
 *      mounted --- file not found, image doesn't follow
 *      the expected format (image size = 4mb, sectorsize = 512,
 *      sectors per cluster = 1, sectors per fat = 32, number of FATs = 2
 *      total_sectors = 0x2000).
 *   - Pointer to an opaque struct representing the mounted
 *      disk image. You can store whatever you need to store
 *      in this struct.
 */
cat16_volume_t *cat16_mount(const char *image_path){
    cat16_volume_t *volume = malloc(sizeof(cat16_volume_t));
    FILE *file = fopen(image_path, "rb+");
    // image could not be mounted | no such file or directory
    if (file == NULL){
        volume->last_error_code = ENOENT;
        free(volume);
        return NULL;
    }
    // determine the size of the image 
    fseek(file, 0, SEEK_END);
    volume->image_size = ftell(file); // current possiton of pointer in the stream. since I'm at the end of the file, this will give us the size of the filev
    // store the image buffer in the volume struct
    volume->image_buffer = file;
    // fseek moves file pointer to specified position in the file. 
    // SEEK_SET goes to the absolute position in the file. 
    // fread (where to store data, size of the data, number of times to read data, where its stored) | reads raw bytes data from a file and sends to a buffer. 
    // jump around to fill in the struct with the correct values
    fseek(file, 11, SEEK_SET);
    fread(&volume->sector_size, sizeof(volume->sector_size), 1, file);
    fseek(file, 13, SEEK_SET);
    fread(&volume->sectors_per_cluster, sizeof(volume->sectors_per_cluster), 1, file);
    fseek(file, 22, SEEK_SET);
    fread(&volume->sectors_per_fat, sizeof(volume->sectors_per_fat), 1, file);
    fseek(file, 16, SEEK_SET);
    fread(&volume->number_fats, sizeof(volume->number_fats), 1, file);
    fseek(file, 19, SEEK_SET);
    fread(&volume->total_sectors, sizeof(volume->total_sectors), 1, file);
    // check that the image meets the expected format
    if (volume->image_size != FOUR_MB || volume->sector_size != SECTOR_SIZE || volume->sectors_per_cluster != SECTORS_PER_CLUSTER || volume->sectors_per_fat != SECTORS_PER_FAT || volume->number_fats != 2 || volume->total_sectors != TOTAL_SECTORS){
        fclose(file);
        free(volume);
        return NULL;
    }
    return volume;
}

/*
 * cat16_unmount -- unmounts a volume object previously
 *      returned from cat16_mount(). It should close any open
 *      file descriptors and free any memory associated with
 *      the mounted volume, including the memory for the
 *      volume struct.
 * Return:
 *  - 0 (zero) on success
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code into the
 *      cat16_strerror() function, in order to get the
 *      corresponding error message. The standard system
 *      error message should be used for EINVAL.
 */
int cat16_unmount(cat16_volume_t *volume){
    // invaild argument
    if (volume == NULL){
        return -EINVAL;
    }
    fclose(volume->image_buffer);
    free(volume);
    return 0;
}


/*
 * cat16_readfile -- read a file with the specified name
 *      (path) from the mounted volume.
 *      (bytes_to_write) and content (filedata) specified.
 *      If a file with that name already exists, it should be
 *      overwritten, and the old file data removed.
 * Return:
 *  - On success, it should return a pointer to the bytes
 *      read from the file (the file's contents) in a block
 *      of memory allocated on the heap. You should also
 *      store the number of bytes read in the memory pointed
 *      to by the "bytes_read" argument.
 *
 *  - On failure, it should return NULL, and you should store
 *      a negative error code in the volume somewhere, so
 *      that I can get the error message by calling
 *      cat16_vstrerror(). The standard system error message
 *      should be used for EINVAL, ENAMETOOLONG,
 *      ENOENT, EISDIR, and ENOTDIR.
 */
uint8_t *cat16_readfile(cat16_volume_t *volume, const char *path, size_t *bytes_read){
    // check for invalid args 
    if (volume == NULL || path == NULL || bytes_read == NULL){
        volume->last_error_code = -EINVAL;
        return NULL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry, 0);
    // check for errors when searching for the file
    if (result < 0){
        volume->last_error_code = result;
        return NULL;
    }
    // found a directory instead of a file
    if (entry.attributes & 0x10){ 
        volume->last_error_code = -EISDIR;
        return NULL;
    }
    // reading starts here
    uint8_t *buffer = malloc(entry.file_size);
    int remaining_bytes = entry.file_size;
    int offset = 0;
    uint16_t cluster = entry.first_logical_cluster;
    int sector = 97 + (cluster - 2);
    while (remaining_bytes > 0){
        offset = sector * SECTOR_SIZE;
        fseek(volume->image_buffer, offset, SEEK_SET);
        if (remaining_bytes >= SECTOR_SIZE){
            fread(buffer + (entry.file_size - remaining_bytes), SECTOR_SIZE, 1, volume->image_buffer);
            remaining_bytes -= SECTOR_SIZE;
        }
        else{
            fread(buffer + (entry.file_size - remaining_bytes), remaining_bytes, 1, volume->image_buffer);
            remaining_bytes = 0;
        }
        // find next logical cluster
        // go to start of the fat 
        // I can jump to the first cluster then just follow jumping around 
        fseek(volume->image_buffer, (SECTOR_SIZE * 1) + (cluster * sizeof(cluster)), SEEK_SET); // FAT1 STARTS AT SECTOR 1
        fread(&cluster, sizeof(cluster), 1, volume->image_buffer);
        sector = 97 + (cluster - 2);
    }
    *bytes_read = entry.file_size;
    return buffer;
}

/*
 * cat16_unlink -- unlink the file specified by path on the
 *      volume. This function should follow the approach we
 *      discussed in class. Mark the directory entry as
 *      deleted, and zero out its FAT entries. Leave the
 *      rest of its information (directory entry and
 *      contents) alone.
 * Return:
 *  - On success, it should return zero (0).
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code to
 *      cat16_strerror() to retrieve the corresponding error
 *      message. The standard system error messages
 *      should be used for EINVAL, ENAMETOOLONG,
 *      ENOENT, and EISDIR.
 */
int cat16_unlink(cat16_volume_t *volume, const char *path){
    // check for invalid args 
    if (volume == NULL || path == NULL){
        volume->last_error_code = -EINVAL;
        return -EINVAL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry, 0);
    // check for errors when searching for the file
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }
    // found a directory instead of a file
    if (entry.attributes & 0x10){ 
        volume->last_error_code = -EISDIR;
        return -EISDIR;
    }
    // set every entry in the fat chain to 0x0000 and mark the directory entry as deleted
    entry.filename[0] = 0xE5; // mark the directory entry as deleted
    fseek(volume->image_buffer, entry.byte_offset, SEEK_SET);
    fwrite(entry.filename, sizeof(entry.filename), 1, volume->image_buffer);
    uint16_t cluster = entry.first_logical_cluster;
    while (cluster  >= 0x0002 && cluster <= 0xFFEF){
        // find next logical cluster
        // go to start of the fat 
        // I can jump to the first cluster then just follow jumping around 
        int fat1_offset = (SECTOR_SIZE * 1) + (cluster * sizeof(cluster));
        int fat2_offset = (SECTOR_SIZE * 33) + (cluster * sizeof(cluster));

        //find next logical cluster
        fseek(volume->image_buffer, fat1_offset, SEEK_SET);
        fread(&cluster, sizeof(cluster), 1, volume->image_buffer);
        // zero out the fat entries in both FAT1 and FAT2
        uint16_t zero = 0x0000;
        fseek(volume->image_buffer, fat1_offset, SEEK_SET);
        fwrite(&zero, sizeof(zero), 1, volume->image_buffer);
        fseek(volume->image_buffer, fat2_offset, SEEK_SET);
        fwrite(&zero, sizeof(zero), 1, volume->image_buffer);
        // if the next logical cluster is 0xFFFF then I have reached the end of the file
        if (cluster == 0xFFFF){
            fseek(volume->image_buffer, fat1_offset, SEEK_SET);
            fwrite(&zero, sizeof(zero), 1, volume->image_buffer);
            fseek(volume->image_buffer, fat2_offset, SEEK_SET);
            fwrite(&zero, sizeof(zero), 1, volume->image_buffer);
            break;
        }
    }
    return 0;
}

// to write a new file just sequentially search through clusters not in use
// if file already exists use any many allocated clusters as possible then add sqeuentiall
// alternatively you can unlink then the code is the same as 1. 




/*
 * cat16_writefile -- write a file with the specified name
 *      (path) to the mounted volume. If successful, the
 *      newly created file should have the length
 *      (bytes_to_write) and content (filedata) specified.
 *      If a file with that name already exists, it should be
 *      overwritten, and the old file data removed.
 * Return:
 *  - 0 (zero) on success.
 *  - On failures, it should return a negative error code.
 *      I should be able to pass this error code into the
 *      cat16_strerror() function, in order to get the
 *      corresponding error message. The standard system
 *      error message should be used for ENOSPC, EINVAL,
 *      ENAMETOOLONG, ENOENT, and ENOTDIR.
 */
int cat16_writefile(cat16_volume_t *volume, const char *path, uint8_t *filedata, size_t bytes_to_write){
    // check for invalid args 
    if (volume == NULL || path == NULL || filedata == NULL){
        volume->last_error_code = -EINVAL;
        return -EINVAL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry, 1);
    // check for errors when searching for the file
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }
    // found a directory instead of a file
    if (entry.entry_found == 1 && entry.attributes & 0x10){ 
        volume->last_error_code = -EISDIR;
        return -EISDIR;
    }
    // writing starts here two cases 
    // case 1: file already exists, unlink it and write new data
    if (entry.entry_found == 1){
        cat16_unlink(volume, path);
    }

    // after unlinking. case 1 is same as case 2 file does not exist

    // get the file name from the path
    const char *filename_ptr = strrchr(path, '/'); // jump to last / in path 
    if (filename_ptr != NULL){
        filename_ptr++; //skip /
    }

    int pos = 0; //track where we are on the file name path
    int i = 0; // track position in entry.filename
    while (filename_ptr[pos] != '\0' && filename_ptr[pos] != '.' && i < filename_bytes){
        entry.filename[i] = filename_ptr[pos];
        i++;
        pos++;
    }
    while (i < filename_bytes){
        entry.filename[i] = ' '; // pad with spaces
        i++;
    }
    if (filename_ptr[pos] != '.' && filename_ptr[pos] != '\0'){
        volume->last_error_code = -ENAMETOOLONG;
        return -ENAMETOOLONG;
    }
    if (filename_ptr[pos] == '.'){
        pos++; // skip .
    }
    i = 0; // rest for extension now
    while (filename_ptr[pos] != '\0' && i < extension_bytes){
        entry.extension[i] = filename_ptr[pos];
        i++;
        pos++;
    }
    while (i < extension_bytes){
        entry.extension[i] = ' ';
        i++;
    }
    if (filename_ptr[pos] != '\0'){
        volume->last_error_code = -ENAMETOOLONG;
        return -ENAMETOOLONG;
    }

    // write the file to the image buffer and update the FAT table and dir entry

    int remaining_bytes = bytes_to_write;
    int offset = 0;
    uint16_t cluster = find_free_cluster(volume);
    if (cluster == 0xFFFF){
        volume->last_error_code = -ENOSPC;
        return -ENOSPC;
    }
    write_fat_entry(volume, cluster, 0xFFFF); // mark the first cluster as end of file in the FAT
    uint16_t first_logical_cluster = cluster;
    int sector = 97 + (cluster - 2);
    while (remaining_bytes > 0){
        offset = sector * SECTOR_SIZE;
        fseek(volume->image_buffer, offset, SEEK_SET);
        if (remaining_bytes >= SECTOR_SIZE){
            fwrite(filedata + (bytes_to_write - remaining_bytes), SECTOR_SIZE, 1, volume->image_buffer);
            remaining_bytes -= SECTOR_SIZE; 
        }
        else{
            fwrite(filedata + (bytes_to_write - remaining_bytes), remaining_bytes, 1, volume->image_buffer);
            remaining_bytes = 0;
        }
        if (remaining_bytes > 0){
            uint16_t next_cluster = find_free_cluster(volume);
            if (next_cluster == 0xFFFF){
                volume->last_error_code = -ENOSPC;
                return -ENOSPC;
            }
            write_fat_entry(volume, cluster, next_cluster); // link the current cluster to the next cluster in the FAT
            write_fat_entry(volume, next_cluster, 0xFFFF); // mark the next cluster as end of file in the FAT
            cluster = next_cluster;
            sector = 97 + (cluster - 2);
        }
    }
    int dir_write_result = write_dir_entry(volume, &entry, 0, first_logical_cluster, bytes_to_write); // do files have an attribute?
    if (dir_write_result < 0){
        volume->last_error_code = dir_write_result;
        return dir_write_result;
    }
    return 0;
}

 



/*
 * cat16_listdir -- retrieve the contents (the entries) of
 *      the directory specified by the path argument on the
 *      specified mounted volume.
 * Return:
 *  - On success, it should return a pointer to a
 *      heap-allocated array of cat16_dirent_t structs, each
 *      representing a single directory entry. You should
 *      also store the number of entries returned in the
 *      memory pointed to by the "entries_read" argument.
 *      you should not include the "." and ".." entries in
 *      subdirectories.
 *
 *  - On failure, it should return NULL, and you should
 *      store a negative error code in the volume somewhere,
 *      so that I can get the error message by calling
 *      cat16_vstrerror(). The standard system error messages
 *      should be used for EINVAL, ENAMETOOLONG, ENOENT, and
 *      ENOTDIR.
 */
cat16_dirent_t *cat16_listdir(cat16_volume_t *volume, const char *path, size_t *entries_read){
    // check for invalid args
    if (volume == NULL || path == NULL || entries_read == NULL){
        volume->last_error_code = -EINVAL;
        return NULL;
    }
    //allocate memory for the directory entries
    cat16_dirent_t *dir_entries = malloc(sizeof(cat16_dirent_t) * path_depth);
    if (dir_entries == NULL){
        volume->last_error_code = -ENOMEM;
        return NULL;
    }
    size_t num_entries = 0;
    int current_sector = ROOT_DIR_START_SECTOR; 
    int end_sector = ROOT_DIR_END_SECTOR; 
    const char *path_ptr = path; // so I can move through the path without modifying the original string
    // skip leading slashs
    while (*path_ptr == '/'){
        path_ptr++;
    }
    // search each entry
    while (*path_ptr != '\0'){
        size_t len = strcspn(path_ptr, "/"); // len of string up to / 
        char entry_name[13]; // 8 + . + 3 + null terminator
        // filename too long
        if (len > sizeof(entry_name)){
            volume->last_error_code = -ENAMETOOLONG;
            return NULL; 
        }
        strncpy(entry_name, path_ptr, len);
        entry_name[len] = '\0'; 
        path_ptr += len; 
        //skip slashes 
        while(*path_ptr == '/'){
            path_ptr++;
        }
        cat16_dir_entry_t entry = search_directory(volume, entry_name, current_sector, end_sector);
        // no dir/file found
        if (entry.entry_found == 0){
            volume->last_error_code = -ENOENT;
            return NULL;
        }
        //is it a directory
        if(!(entry.attributes & 0x10)){
            volume->last_error_code = -ENOTDIR;
            return NULL;
        }
        // end of path reached
        if (*path_ptr == '\0'){
            *entries_read = num_entries;
            return dir_entries; 
        }
        //fill in cat16_dirent_t struct with the information from the cat16_dir_entry_t struc
        cat16_dirent_t dir_entry;
        strcpy(dir_entry.name, entry_name);
        dir_entry.attributes = entry.attributes;
        dir_entry.size = entry.file_size;
        dir_entry.deleted = entry.deleted;
        dir_entries[num_entries] = dir_entry;
        num_entries++;

        // update the current sector to the first logical cluster of the found directory
        current_sector = 97 + (entry.first_logical_cluster - 2);
        end_sector = current_sector + 1;
    }
    *entries_read = num_entries;
    return dir_entries;
}

/*
 * cat16_stat -- retrieve information about the file or
 *      directory pointed to by path on the specified volume.
 * Return:
 *  - On success, it should return zero (0). You should
 *      also store the requested information into the
 *      memory pointed to by the "out_stat" argument.
 *
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code to
 *      cat16_strerror() to retrieve the corresponding error
 *      message. The standard system error messages
 *      should be used for EINVAL, ENAMETOOLONG, ENOENT, and
 *      ENOTDIR. why would I throw a not dir error if I am loooking for a path or a directory?
 */
int cat16_stat(cat16_volume_t *volume, const char *path, cat16_stat_t *out_stat){
    // check for invalid args 
    if (volume == NULL || path == NULL || out_stat == NULL){
        volume->last_error_code = -EINVAL;
        return -EINVAL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry, 0);
    // check for errors when searching for the file
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }
    out_stat->attributes = entry.attributes;
    out_stat->size = entry.file_size;
    out_stat->first_cluster = entry.first_logical_cluster;
    return 0;

}
/*
 * cat16_mkdir -- create a new directory with the specified
 *      name (path) on the volume. If the directory already
 *      exists don't overwrite it (EEXIST).
 * Return:
 *  - On success, it should return zero (0).
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code to
 *      cat16_strerror() to retrieve the corresponding error
 *      message. The standard system error messages
 *      should be used for EEXIST, EINVAL, ENAMETOOLONG,
 *      ENOENT, and ENOTDIR.
 */
int cat16_mkdir(cat16_volume_t *volume, const char *path){
    //     // check for invalid args 
    // if (volume == NULL || path == NULL){
    //     volume->last_error_code = -EINVAL;
    //     return -EINVAL;
    // }
    // // search for the file 
    // cat16_dir_entry_t entry;
    // int result = path_handler(volume, path, &entry, 0);
    // // check for errors when searching for the file
    // if (result < 0){
    //     volume->last_error_code = result;
    //     return result;
    // }
    // // found a file instead of a directory
    // if (!(entry.attributes & 0x10)){ 
    //     volume->last_error_code = -ENOTDIR;
    //     return -ENOTDIR;
    // }
    // // directory already exists
    // if (entry.entry_found == 1){
    //     volume->last_error_code = -EEXIST;
    //     return -EEXIST;
    // }
    // //write dir to the disk
    // uint16_t cluster = 0;
    // // find first available logical cluster
    // for (uint16_t i = 0x0002; i <= 0xFFEF; i++){
    //     fseek(volume->image_buffer, (SECTOR_SIZE * 1) + (i * sizeof(i)), SEEK_SET);
    //     fread(&cluster, sizeof(cluster), 1, volume->image_buffer);
    //     if (cluster == 0x0000){
    //         cluster = i;
    //         break;
    //     }
    //     // if I am at the end of the FAT and I have not found an available cluster then I'm out of space
    //     if (i == 0xFFEF && cluster == 0x0000){
    //         volume->last_error_code = -ENOSPC;
    //         return -ENOSPC;
    //     }
    // } 
    // int sector = 97 + (cluster - 2);
    // int offset = sector * SECTOR_SIZE;

    return 0;
}



/*
 * cat16_rmdir -- removes a directory with the specified
 *      name (path) on the volume. This function should
 *      follow the approach we discussed in class. Mark the
 *      directory entry as deleted, and zero out its FAT
 *      entries. Leave the rest of its information
 *      (directory entry and contents) alone.
 * Return:
 *  - On success, it should return zero (0).
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code to
 *      cat16_strerror() to retrieve the corresponding error
 *      message. The standard system error messages
 *      should be used for EINVAL, ENAMETOOLONG,
 *      ENOENT, ENOTDIR, and ENOTEMPTY.
 */
int cat16_rmdir(cat16_volume_t *volume, const char *path){
    return 0;
}
