/*
 * zerofree - a tool to zero free blocks in an ext2 filesystem
 *
 * Copyright (C) 2004 R M Yorston
 *
 * This file may be redistributed under the terms of the GNU General Public
 * License.
 */

#include <ext2fs/ext2fs.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define USAGE "usage: %s [-n] [-v] filesystem\n"

int main(int argc, char **argv)
{
	errcode_t ret ;
	int flags ;
	int superblock = 0 ;
	int open_flags = EXT2_FLAG_RW ;
	int blocksize = 0 ;
	ext2_filsys current_fs = NULL;
	unsigned long blk ;
	unsigned char *buf;
	unsigned char *empty;
	int i, c ;
	unsigned int free, nonzero ;
	int verbose = 0 ;
	int dryrun = 0 ;

	while ( (c=getopt(argc, argv, "nv")) != -1 ) {
		switch (c) {
		case 'n' :
			dryrun = 1 ;
			break ;
		case 'v' :
			verbose = 1 ;
			break ;
		default :
			fprintf(stderr, USAGE, argv[0]) ;
			return 1 ;
		}
	}

	if ( argc != optind+1 ) {
		fprintf(stderr, USAGE, argv[0]) ;
		return 1 ;
	}

	ret = ext2fs_check_if_mounted(argv[optind], &flags) ;
	if ( ret ) {
		fprintf(stderr, "%s: failed to determine filesystem mount state  %s\n",
					argv[0], argv[optind]) ;
		return 1 ;
	}

	if ( flags & EXT2_MF_MOUNTED ) {
		fprintf(stderr, "%s: filesystem %s is mounted\n",
					argv[0], argv[optind]) ;
		return 1 ;
	}

	ret = ext2fs_open(argv[optind], open_flags, superblock, blocksize,
							unix_io_manager, &current_fs);
	if ( ret ) {
		fprintf(stderr, "%s: failed to open filesystem %s\n",
					argv[0], argv[optind]) ;
		return 1 ;
	}

	empty = (unsigned char *)calloc(1, current_fs->blocksize) ;
	buf = (unsigned char *)malloc(current_fs->blocksize) ;

	if ( empty == NULL || buf == NULL ) {
		fprintf(stderr, "%s: out of memory (surely not?)\n", argv[0]) ;
		return 1 ;
	}

	ret = ext2fs_read_inode_bitmap(current_fs);
	if ( ret ) {
		fprintf(stderr, "%s: error while reading inode bitmap\n", argv[0]);
		return 1 ;
	}

	ret = ext2fs_read_block_bitmap(current_fs);
	if ( ret ) {
		fprintf(stderr, "%s: error while reading block bitmap\n", argv[0]);
		return 1 ;
	}

	free = nonzero = 0 ;
	for ( blk=current_fs->super->s_first_data_block;
			blk < current_fs->super->s_blocks_count; blk++ ) {

		if ( ext2fs_test_block_bitmap(current_fs->block_map, blk) ) {
			continue ;
		}

		++free ;

		ret = io_channel_read_blk(current_fs->io, blk, 1, buf);
		if ( ret ) {
			fprintf(stderr, "%s: error while reading block\n", argv[0]) ;
			return 1 ;
		}

		for ( i=0; i < current_fs->blocksize; ++i ) {
			if ( buf[i] ) {
				break ;
			}
		}

		if ( i == current_fs->blocksize ) {
			continue ;
		}

		++nonzero ;

		if ( !dryrun ) {
			ret = io_channel_write_blk(current_fs->io, blk, 1, empty) ;
			if ( ret ) {
				fprintf(stderr, "%s: error while writing block\n", argv[0]) ;
				return 1 ;
			}
		}
	}

	if ( verbose ) {
		printf("%u/%u/%u\n", nonzero, free,
				current_fs->super->s_blocks_count) ;
	}

	ret = ext2fs_close(current_fs) ;
	if ( ret ) {
		fprintf(stderr, "%s: error while closing filesystem\n", argv[0]) ;
		return 1 ;
	}

	return 0 ;
}
