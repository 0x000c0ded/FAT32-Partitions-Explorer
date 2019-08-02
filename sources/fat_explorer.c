#include "fat_explorer.h"

Directory_Record* init_directory_record() {
	Directory_Record* record = (Directory_Record*) malloc(sizeof(Directory_Record));
	for (int i = 0; i < 13; ++i) record->short_file_name[i] = '\0';
	for (int i = 0; i < 260; ++i) record->long_file_name[i] = L'\0';
	return record;
}

wchar_t cast_utf16(char16_t c) {
	return (wchar_t)c;
}

void convert_utf16_to_wchar(byte* str, int str_len, wchar_t* w_str, int* wstr_len) {
	char* wstr_ptr = (char*) w_str;
	if (*wstr_len >= str_len) *wstr_len = str_len;
	for (int i = 0; i < *wstr_len; ++i) {
		w_str[i] = cast_utf16(*(char16_t*)(str + 2 * i));
	}
}

// cluster pointer should e pointing to already allocated memory of sufficient size
bool load_cluster(FILE* disk, FAT32VolumeID* volume_id, int cluster_num, byte* cluster) {
	int cluster_lba = volume_id->clusters_begin_lba + volume_id->sectors_per_cluster * (cluster_num - 2);
	int cluster_offset = SECTOR_SIZE * cluster_lba;
	int n;
	n = fseek(disk, cluster_offset, SEEK_SET);
	if (n != 0) {
		printf("Error seeking position %d \n", cluster_offset);
		return false;
	}
	n = fread(cluster, SECTOR_SIZE, volume_id->sectors_per_cluster, disk);
	if (n <= 0) {
		printf("Error reading from disk file.\n");
		return false;
	}
	return true;
}

// sector pointer should e pointing to already allocated memory of sufficient size
bool load_sector(FILE* disk, FAT32VolumeID* volume_id, int lba_adr, byte* sector) {
	int n = fseek(disk, SECTOR_SIZE * lba_adr, SEEK_SET);
	if (n != 0) {
		printf("Error seeking position %d \n", SECTOR_SIZE * lba_adr);
		return false;
	}
	n = fread(sector, SECTOR_SIZE, 1, disk);
	if (n <= 0) {
		printf("Error reading from disk file.\n");
		return false;
	}
	return true;
}

// TODO : handle the first 446 bytes of MBR sector 
// disk file should be open
MasterBootRecord* load_MBR_sector(FILE* disk) {
	byte buffer[512];
	int n;
	if (disk == NULL) {
		printf("Error : Called with NULL disk file at %s:%d \n", __FILE__, __LINE__);
		exit(-1);
	}
	n = fseek(disk, 0, SEEK_SET);
	if (n != 0) {
		printf("Error : Failed to read MBR sector at %s:%d \n", __FILE__, __LINE__);
		exit(-1);
	}
	n = fread(buffer, SECTOR_SIZE, 1, disk);
	if (n != 1) {
		printf("Error : Failed to read MBR sector at %s:%d \n", __FILE__, __LINE__);
		exit(-1);
	}
	MasterBootRecord* mbr = (MasterBootRecord*)malloc(1 * sizeof(MasterBootRecord));
	for (int i = 0; i < 4; ++i) {
		int offset = PARTITION_MBR_OFFSET + 16 * i;
		mbr->entries[i].state = buffer[offset];
		for (int j = 0; j < 3; ++j) mbr->entries[i].chs_begin[j] = buffer[offset + 1 + j];
		mbr->entries[i].type = buffer[offset + 4];
		for (int j = 0; j < 3; ++j) mbr->entries[i].chs_end[j] = buffer[offset + 5 + j];
		mbr->entries[i].lba_begin = *((int*)(buffer + offset + 8));
		mbr->entries[i].nb_sectors = *((int*)(buffer + offset + 12));
	}
	mbr->signature[0] = buffer[510];
	mbr->signature[1] = buffer[511];
	return mbr;
}

void print_MBR_sector_info(MasterBootRecord* mbr) {
	printf("MBR signature : %x%x\n", mbr->signature[0], mbr->signature[1]);
	for (int i = 0; i < 4; ++i) {
		printf("========================================================================================\n");
		printf("Partition : %d \n", i);
		PartitionEntry* partition = &mbr->entries[i];
		printf("sectors count : %d \n", partition->nb_sectors);
		printf("partition size : %f Gb \n", ((double)partition->nb_sectors) / 2048 / 1024);
		printf("Partition LBA begin address : %d \n", partition->lba_begin);
		printf("partition state : %x \n", partition->state);
		printf("Partition type : %x \n", partition->type);
	}
}

// load FAT32 VolumeID table informations using MBR data
FAT32VolumeID* load_VolumeID_table(FILE* disk, MasterBootRecord* mbr) {
	int partition_start_lda = mbr->entries[0].lba_begin;
	byte buffer[512];
	int n;
	n = fseek(disk, partition_start_lda * SECTOR_SIZE, SEEK_SET);
	if (n != 0) {
		printf("ERROR : failed to seek position %d\n", partition_start_lda * SECTOR_SIZE);
		return NULL;
	}
	n = fread(buffer, 512, 1, disk);
	if (n <= 0) {
		printf("ERROR : failed to read characters from disk file\n");
		return NULL;
	}
	FAT32VolumeID* volume_id = (FAT32VolumeID*) malloc(sizeof(FAT32VolumeID));
	volume_id->bytes_per_sector = *((short*)(buffer + 0xB));
	volume_id->sectors_per_cluster = buffer[0xD];
	volume_id->nb_reserved_sectors = *((short*)(buffer + 0xE));
	volume_id->nb_FAT_tables = buffer[0x10];
	volume_id->sectors_per_FAT = *((int*)(buffer + 0x24));
	volume_id->root_dir_first_cluster = *((int*)(buffer + 0x2c));
	volume_id->signature[0] = buffer[510];
	volume_id->signature[1] = buffer[511];

	volume_id->fat_begin_lba = partition_start_lda + volume_id->nb_reserved_sectors;
	volume_id->clusters_begin_lba = volume_id->fat_begin_lba + volume_id->nb_FAT_tables * volume_id->sectors_per_FAT;
	return volume_id;
}

void print_FAT32_volume_id(FAT32VolumeID* volume_id) {
	printf("========================================================================================\n");
	printf("Displaying VolumeID informations\n");
	printf("Bytes per sector : %d\n", volume_id->bytes_per_sector);
	printf("Sectors per cluster : %d \n", volume_id->sectors_per_cluster);
	printf("Number of reserved sectors : %d \n", volume_id->nb_reserved_sectors);
	printf("Number of FAT tables : %d \n", volume_id->nb_FAT_tables);
	printf("Sectors per FAT table : %d \n", volume_id->sectors_per_FAT);
	printf("Root Directory First Cluster : %d \n", volume_id->root_dir_first_cluster);
	printf("FAT beginning LBA : %ld \n", volume_id->fat_begin_lba);
	printf("Clusters begin LBA : %ld \n", volume_id->clusters_begin_lba);
	printf("Signature : %x%x\n", volume_id->signature[0], volume_id->signature[1]);
}

void read_time(byte* ptr, short time[3]) {
	short* t = (short*)ptr;
	time[2] = (*t << 1) & 0x3f;
	time[1] = (*t >> 5) & 0x3f;
	time[0] = (*t >> 11) & 0x1f;
}

void read_date(byte* ptr, short time[3]) {
	short* d = (short*)ptr;
	time[2] = 1980 + (*d >> 9) & 0x7f;
	time[1] = (*d >> 5) & 0x1f;
	time[0] = (*d) & 0xf;
}

Directory_Entry* read_short_filename_entry(byte buffer[DIRECTORY_ENTRY_SIZE]) {
	if (buffer[0] == FAT32_UNUSED_DIRECTORY_RECORD_MARKER) {
		printf("ERROR : unused record \n");
		return NULL;
	}
	if (buffer[0] == FAT32_DIRECTORY_RECORDS_END_MARKER) {
		printf("ERROR : reading end record \n");
		return NULL;
	}
	if (FAT32_IS_LONG_FILENAME(buffer[11])) {
		printf("ERROR : reading long filename entry as a normal directory record \n");
		return NULL;
	}
	Directory_Entry* directory_record = (Directory_Entry*) malloc(sizeof(Directory_Entry));
	for (int i = 0; i < 8; ++i) directory_record->filename[i] = buffer[i];
	for (int i = 0; i < 3; ++i) directory_record->filename_extension[i] = buffer[8 + i];
	directory_record->file_attribs = buffer[11];
	// time informations 
	read_time(buffer + 14, directory_record->creation_time);
	read_date(buffer + 16, directory_record->creation_date);
	read_date(buffer + 18, directory_record->last_access_date);
	read_time(buffer + 22, directory_record->last_write_time);
	read_date(buffer + 24, directory_record->last_write_date);

	byte* starting_cluster_num = (byte*)&directory_record->starting_cluster_num;
	starting_cluster_num[2] = buffer[20];
	starting_cluster_num[3] = buffer[21];
	starting_cluster_num[0] = buffer[26];
	starting_cluster_num[1] = buffer[27];
	directory_record->file_size = *((int*)(buffer + 28));

	return directory_record;
}

LFN_Directory_Entry* read_long_filename_entry(byte buffer[DIRECTORY_ENTRY_SIZE]) {
	if (buffer[12] != 0 || !FAT32_IS_LONG_FILENAME(buffer[11])/*  || buffer[26] != 0 || buffer[27] != 0*/) {
		printf("ERROR : invalid long filename entry \n");
		return NULL;
	}
	LFN_Directory_Entry* entry = (LFN_Directory_Entry*) malloc(sizeof(LFN_Directory_Entry));
	for (int i = 0; i < 13; ++i) entry->file_name_part[i] = L'\0';
	entry->sequence_number = buffer[0];
	entry->file_attribs = buffer[11];
	entry->SFN_entry_checksum = buffer[13];
	for (int i = 0; i < 5; ++i) entry->file_name_part[i] = cast_utf16(*(char16_t*)(buffer + 1 + 2 * i));
	for (int i = 5; i < 11; ++i) entry->file_name_part[i] = cast_utf16(*(char16_t*)(buffer + 4 + 2 * i));
	for (int i = 11; i < 13; ++i) entry->file_name_part[i] = cast_utf16(*(char16_t*)(buffer + 6 + 2 * i));
	return entry;
}

void print_dir_record(Directory_Record* entry) {
	printf("========================================================================================\n");
	printf("Short file name : %s \n", entry->short_file_name);
	printf("Long file name : %ls \n", entry->long_file_name);
	printf("File size : %d bytes \n", entry->file_size);
	printf("File attributes : %x \n", entry->file_attribs);
	printf("Starting cluster number : %d \n", entry->starting_cluster_num);
	printf("Is directory : %d \n", FAT32_IS_SUBDIRECTORY(entry->file_attribs) ? 1 : 0);
	printf("Number of LFN entries : %d \n", entry->nb_LFN_entries);
	printf("last write date : %d-%d-%d \n", entry->last_write_date[0], entry->last_write_date[1], entry->last_write_date[2]);
	printf("last write time : %d:%d:%d \n", entry->last_write_time[0], entry->last_write_time[1], entry->last_write_time[2]);
	printf("last access date : %d-%d-%d \n", entry->last_access_date[0], entry->last_access_date[1], entry->last_access_date[2]);
	printf("creation date : %d-%d-%d \n", entry->creation_date[0], entry->creation_date[1], entry->creation_date[2]);
	printf("creation time : %d:%d:%d \n", entry->creation_time[0], entry->creation_time[1], entry->creation_time[2]);
	printf("========================================================================================\n");
}

// *nb_clusters is the maximum length of the cluster chain
// cluster_numbers is a previously allocated cluster numbers array
// *nb_clusters will hold the number of the clusters in the chaine
// the 4 ignored MSB of 32bits FAT entries are handled here 
void fetch_cluster_chain(FILE* disk, FAT32VolumeID* volume_id, int start_cluster_number, int* cluster_numbers, int* nb_cluster) {
	int n;
	int current_fat_entry_offset =  SECTOR_SIZE * volume_id->fat_begin_lba + FAT32_ENTRY_SIZE * (start_cluster_number);
	int max_clusters_num = *nb_cluster;
	int current_cluste_num = start_cluster_number;
	*nb_cluster = 0;

	byte sector_buffer[SECTOR_SIZE];
	int sector_lba = current_fat_entry_offset / SECTOR_SIZE;
	n = fseek(disk, SECTOR_SIZE * sector_lba, SEEK_SET);
	if (n != 0) {
		printf("Error seeking position %d \n", SECTOR_SIZE * sector_lba);
		exit(-1);
	}
	n = fread(sector_buffer, SECTOR_SIZE, 1, disk);
	if (n <= 0) {
		printf("Error reading from disk file.\n");
		exit(-1);
	}

	for (int i = 0; i < max_clusters_num; ++i) {
		cluster_numbers[*nb_cluster] = current_cluste_num;
		(*nb_cluster)++;
		
		current_cluste_num = *((int*)(sector_buffer + (current_fat_entry_offset % SECTOR_SIZE))) & 0x0FFFFFFF ;
		if (current_cluste_num >= 0x0FFFFFF8) break;

		current_fat_entry_offset = SECTOR_SIZE * volume_id->fat_begin_lba + FAT32_ENTRY_SIZE * current_cluste_num;
		if (current_fat_entry_offset / SECTOR_SIZE != sector_lba) {
			sector_lba = current_fat_entry_offset / SECTOR_SIZE;
			n = fseek(disk, SECTOR_SIZE * sector_lba, SEEK_SET);
			if (n != 0) {
				printf("Error seeking position %d \n", SECTOR_SIZE * sector_lba);
				exit(-1);
			}
			n = fread(sector_buffer, SECTOR_SIZE, 1, disk);
			if (n <= 0) {
				printf("Error reading from disk file.\n");
				exit(-1);
			}
		}
	}
	return;
}

// create SFN entry checksum
byte create_sum(Directory_Entry* entry) {
    byte sum;
    for (int i = sum = 0; i < 8; i++) sum = (sum >> 1) + (sum << 7) + entry->filename[i];
	for (int i = 0; i < 3; i++) sum = (sum >> 1) + (sum << 7) + entry->filename_extension[i];
    return sum;
}

// directory_records should be allocated previously and directory_records_size contains its size
void fetch_directory_records(FILE* disk, FAT32VolumeID* volume_id, int start_cluster_number, Directory_Record** directory_records, int* directory_records_size) {
	int max_directory_records_size = *directory_records_size;
	int cluster_chain[MAX_CLUSTER_CHAIN_LEN];
	int cluster_chain_len = MAX_CLUSTER_CHAIN_LEN;
	int cluster_size = SECTOR_SIZE * volume_id->sectors_per_cluster;
	// fetch cluster chain from FAT table
	fetch_cluster_chain(disk, volume_id, start_cluster_number, cluster_chain, &cluster_chain_len);
	// load and class all directory entries
	byte* clusters_buffer = (byte*) malloc(cluster_size * cluster_chain_len * sizeof(byte));
	// load all clusters
	for (int i = 0; i < cluster_chain_len; ++i) load_cluster(disk, volume_id, cluster_chain[i], clusters_buffer + i * cluster_size);
	// initialise pending lfn entries
	LFN_Directory_Entry* pending_lfn_entries[20];
	for (int i = 0; i < 20; ++i) pending_lfn_entries[i] = NULL;
	int lfn_entries_count = 0;

	*directory_records_size = 0;
	// read directory records
	int nb_sfn_entries = 0;
	int record_offset_limit = cluster_size * cluster_chain_len;
	for (int record_offset = 0; record_offset < record_offset_limit; record_offset += 32) {
		if (clusters_buffer[record_offset] == FAT32_UNUSED_DIRECTORY_RECORD_MARKER) continue;
		if (clusters_buffer[record_offset] == FAT32_DIRECTORY_RECORDS_END_MARKER) break;;
		// passes if : is not a LFN or if its the last LFN
		// it is possible that the first entry is short (like volume label and backward compatibility with DOS stuff which doesn't have LFN)
		// if the first entry is SFN there won't be any reading of LFN entries
		// if the first entry is LFN one and it's the last sequence (masking its attributes with 0x40 yields true)
		// 		then fetch all next entries untill SFN entry is found 
		bool is_lfn = FAT32_IS_LONG_FILENAME(clusters_buffer[record_offset + 11]);
		bool is_last_lfn = is_lfn && FAT32_IS_LFN_LAST_LONG_ENTRY(clusters_buffer[record_offset]);
		if (is_lfn && !is_last_lfn) {
			printf("Warning : Skipped long entry out of place \n");
			continue;
		}
		lfn_entries_count = 0;
		while (record_offset < record_offset_limit && FAT32_IS_LONG_FILENAME(clusters_buffer[record_offset + 11])) {
			if (pending_lfn_entries[lfn_entries_count] != NULL) free(pending_lfn_entries[lfn_entries_count]);
			pending_lfn_entries[lfn_entries_count] = read_long_filename_entry(clusters_buffer + record_offset);
			lfn_entries_count++;
			record_offset += 32;
		}
		if (record_offset >= record_offset_limit) {
			printf("Warning : exceeded record offset (you should implement this shit to span over clusters you dump :)\n");
			break;
		}

		if (!FAT32_IS_LONG_FILENAME(clusters_buffer[record_offset + 11])) {
			Directory_Record* dir_record = init_directory_record();
			// load data from SFN entry
			Directory_Entry* sfn_entry = read_short_filename_entry(clusters_buffer + record_offset);
			dir_record->file_attribs = sfn_entry->file_attribs;
			dir_record->starting_cluster_num = sfn_entry->starting_cluster_num;
			for (int j = 0; j < 3; ++j) {
				dir_record->last_access_date[j] = sfn_entry->last_access_date[j];
				dir_record->last_write_date[j] = sfn_entry->last_write_date[j];
				dir_record->last_write_time[j] = sfn_entry->last_write_time[j];
				dir_record->creation_date[j] = sfn_entry->creation_date[j];
				dir_record->creation_time[j] = sfn_entry->creation_time[j];
			}
			dir_record->file_size = sfn_entry->file_size;
			for (int j = 0; j < 8; ++j) dir_record->short_file_name[j] = sfn_entry->filename[j];
			dir_record->short_file_name[8] = '.';
			for (int j = 0; j < 3; ++j) dir_record->short_file_name[9 + j] = sfn_entry->filename_extension[j];
			dir_record->short_file_name[12] = '\0';
			dir_record->is_directory = FAT32_IS_SUBDIRECTORY(dir_record->file_attribs);
			dir_record->cluster_offset = record_offset % cluster_size;
			dir_record->cluster_num = cluster_chain[record_offset / cluster_size];
			byte sfn_checksum = create_sum(sfn_entry);
			free(sfn_entry);
			//

			dir_record->nb_LFN_entries = lfn_entries_count;
			for (int i = 0; i < lfn_entries_count; ++i) {
				if (pending_lfn_entries[i]->SFN_entry_checksum != sfn_checksum) {
					printf("Warning : skipped LFN entry with unvalid checksum \n");
					dir_record->nb_LFN_entries--;
					continue;
				}
				int seq_number = pending_lfn_entries[i]->sequence_number & (~0x40);
				for (int j = 0; j < 13; ++j) {
					dir_record->long_file_name[13 * (seq_number - 1) + j] = pending_lfn_entries[i]->file_name_part[j];
				}
			}
			directory_records[(*directory_records_size)++] = dir_record;
		}
	}
	for (int i = 0; i < 20; ++i) {
		if (pending_lfn_entries[i] != NULL) free(pending_lfn_entries[i]);
	}
	for (int i = 0; i < cluster_chain_len; ++i) free(clusters_buffer + i * (SECTOR_SIZE * volume_id->sectors_per_cluster));
}

FileSystem_Node* init_FAT32_filesystem_node() {
	FileSystem_Node* node = (FileSystem_Node*) malloc(sizeof(FileSystem_Node));
	for (int i = 0; i < FAT32_ROOT_DIR_MAX_SIZE; ++i) node->directories_records[i] = NULL;
	for (int i = 0; i < FAT32_ROOT_DIR_MAX_SIZE; ++i) node->directories_nodes[i] = NULL;
	node->nb_subdirectories = 0;
	for (int i = 0; i < FAT32_ROOT_DIR_MAX_SIZE; ++i) node->files_records[i] = NULL;
	node->nb_files = 0;
	node->parent = NULL;
	node->current = node;
	return node;
}

FAT32_FileSystem_Handle* init_FAT32_filesystem_handle() {
	FAT32_FileSystem_Handle* fs_handle = (FAT32_FileSystem_Handle*) malloc(sizeof(FAT32_FileSystem_Handle));
	fs_handle->disk = NULL;
	fs_handle->volume_id = NULL;
	fs_handle->fs_node = init_FAT32_filesystem_node();
	fs_handle->volume_label = NULL;
	return fs_handle;
}

FAT32_FileSystem_Handle* load_FAT32_filesystem_root(FILE* disk, FAT32VolumeID* volume_id) {
	FAT32_FileSystem_Handle* fs_handle = init_FAT32_filesystem_handle();
	fs_handle->disk = disk;
	fs_handle->volume_id = volume_id;
	FileSystem_Node* root_node = fs_handle->fs_node;
	root_node->parent = root_node;
	root_node->current = root_node;
	Directory_Record* directory_records[FAT32_ROOT_DIR_MAX_SIZE];
	int directory_records_size = FAT32_ROOT_DIR_MAX_SIZE;
	root_node->cluster_num = 2;
	fetch_directory_records(disk, volume_id, 2, directory_records, &directory_records_size);
	for (int i = 0; i < directory_records_size; ++i) {
		if (FAT32_IS_VOLUME_LABEL(directory_records[i]->file_attribs)) {
			fs_handle->volume_label = directory_records[i];
		} else if (FAT32_IS_SUBDIRECTORY(directory_records[i]->file_attribs)) {
			root_node->directories_records[root_node->nb_subdirectories++] = directory_records[i];
		} else {
			root_node->files_records[root_node->nb_files++] = directory_records[i];
		}
	}
	return fs_handle;
}

void load_subdirectory(FAT32_FileSystem_Handle* fs_handle, FileSystem_Node* node, int subdir_num) {
	if (subdir_num >= node->nb_subdirectories) {
		printf("Error : Requested non existant subdirectory : %d\n", subdir_num);
		return;
	}
	if (node->directories_nodes[subdir_num] != NULL) return;
	node->directories_nodes[subdir_num] = init_FAT32_filesystem_node();
	node->directories_nodes[subdir_num]->parent = node;
	FileSystem_Node* n = node->directories_nodes[subdir_num];

	Directory_Record* directory_records[FAT32_ROOT_DIR_MAX_SIZE];
	int directory_records_size = FAT32_ROOT_DIR_MAX_SIZE;
	n->cluster_num = node->directories_records[subdir_num]->starting_cluster_num;
	fetch_directory_records(fs_handle->disk, fs_handle->volume_id, node->directories_records[subdir_num]->starting_cluster_num, directory_records, &directory_records_size);
	for (int i = 0; i < directory_records_size; ++i) {
		if (FAT32_IS_SUBDIRECTORY(directory_records[i]->file_attribs)) {
			n->directories_records[n->nb_subdirectories++] = directory_records[i];
		} else {
			n->files_records[n->nb_files++] = directory_records[i];
		}
	}
}

#define LINE_SIZE 32
void print_sector(int lba_adr, byte* buffer) {
	for (int i = 0; i < SECTOR_SIZE; ++i) {
		if (i % LINE_SIZE == 0) printf("%.8X ", lba_adr * SECTOR_SIZE + i);
		printf("%.2X ", buffer[i]);
		if (i % LINE_SIZE == (LINE_SIZE - 1)) printf("\n");
	}
}

void delete_file(FAT32_FileSystem_Handle* fs_handle, FileSystem_Node* curent_dir, int file_num) {
	Directory_Record* file_record = curent_dir->files_records[file_num];
}