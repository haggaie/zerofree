/*
 * sparsify - a tool to make files on an ext2 filesystem sparse
 *
 * Copyright (C) 2004-2012 R M Yorston
 *
 * This file may be redistributed under the terms of the GNU General Public
 * License.
 */
#include <ext2fs/ext2fs.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define USAGE "usage: %s [-n] [-v] filesystem filename ...\n"

struct process_data {
	struct ext2_inode *inode ;
	unsigned char *buf;
	int verbose;
	int dryrun ;
	blk_t count;
	blk_t blocks;
	blk_t total_blocks;
	int old_percent;
} ;

static int process(ext2_filsys fs, blk_t *blocknr, e2_blkcnt_t blockcnt,
					blk_t ref_block, int ref_offset, void *priv)
{
	struct process_data *p ;
	errcode_t ret ;
	int i, group ;

	p = (struct process_data *)priv ;

	p->blocks++;
	if ( blockcnt >= 0 ) {
		ret = io_channel_read_blk(fs->io, *blocknr, 1, p->buf);
		if ( ret ) {
			return BLOCK_ABORT ;
		}

		for ( i=0; i < fs->blocksize; ++i ) {
			if ( p->buf[i] ) {
				break ;
			}
		}

		if ( i == fs->blocksize ) {
			p->count++;
			
			if ( !p->dryrun ) {
				ext2fs_unmark_block_bitmap(fs->block_map, *blocknr) ;
				group = ext2fs_group_of_blk(fs, *blocknr);
				fs->group_desc[group].bg_free_blocks_count++;
				fs->super->s_free_blocks_count++ ;
				/* the inode counts blocks of 512 bytes */
				p->inode->i_blocks  -= fs->blocksize / 512 ;
				*blocknr = 0 ;
				/* direct blocks need to be zeroed in the inode */
				if ( blockcnt < EXT2_NDIR_BLOCKS ) {
					p->inode->i_block[blockcnt] = 0 ;
				}
				return BLOCK_CHANGED ;
			}
		}

		if ( p->verbose ) {
			double percent;

			percent = 100.0 * (double)p->blocks/(double)p->total_blocks;

			if ( (int)(percent*10) != p->old_percent ) {
				fprintf(stderr, "\r%4.1f%%", percent);
				p->old_percent = (int)(percent*10);
			}
		}
	}

	return 0 ;
}

int main(int argc, char **argv)
{
	int verbose = 0 ;
	int dryrun = 0 ;
	errcode_t ret ;
	int flags ;
	int superblock = 0 ;
	int open_flags = EXT2_FLAG_RW ;
	int iter_flags = 0;
	int blocksize = 0 ;
	ext2_filsys current_fs = NULL;
	struct ext2_inode inode ;
	ext2_ino_t root, cwd, inum ;
	int i, c ;
	struct process_data pdata ;

	while ( (c=getopt(argc, argv, "nv")) != -1 ) {
		switch (c) {
		case 'n' :
			dryrun = 1 ;
#if defined(BLOCK_FLAG_READ_ONLY)
			iter_flags |= BLOCK_FLAG_READ_ONLY;
#endif
			break ;
		case 'v' :
			verbose = 1 ;
			break ;
		default :
			fprintf(stderr, USAGE, argv[0]) ;
			return 1 ;
		}
	}

	if ( argc < optind+2 ) {
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

	pdata.buf = (unsigned char *)malloc(current_fs->blocksize) ;
	if ( pdata.buf == NULL ) {
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

	root = cwd = EXT2_ROOT_INO ;

	for ( i=optind+1; i<argc; ++i ) {
		ret = ext2fs_namei(current_fs, root, cwd, argv[i], &inum) ;
		if ( ret ) {
			fprintf(stderr, "%s: failed to find file %s\n", argv[0], argv[i]) ;
			continue ;
		}

		ret = ext2fs_read_inode(current_fs, inum, &inode) ;
		if ( ret ) {
			fprintf(stderr, "%s: failed to open inode %d\n", argv[0], inum) ;
			continue ;
		}

		if ( !ext2fs_inode_has_valid_blocks(&inode) ) {
			fprintf(stderr, "%s: file %s has no valid blocks\n", argv[0],
					argv[i]) ;
			continue ;
		}

#if defined(EXT4_EXTENTS_FL)
		if ( inode.i_flags & EXT4_EXTENTS_FL ) {
			fprintf(stderr, "%s: unable to process %s, it uses extents\n",
					argv[0], argv[i]);
			continue;
		}
#endif

		if ( verbose ) {
			printf("processing %s\n", argv[i]) ;
		}

		pdata.inode = &inode ;
		pdata.verbose = verbose;
		pdata.dryrun = dryrun ;
		pdata.count = pdata.blocks = 0;
		pdata.total_blocks = inode.i_blocks/(current_fs->blocksize >> 9);
		pdata.old_percent = 1000;
		ret = ext2fs_block_iterate2(current_fs, inum, iter_flags, NULL,
				process, &pdata) ;
		if ( ret ) {
			fprintf(stderr, "%s: failed to process file %s\n", argv[0],
					argv[i]) ;
			continue ;
		}

		if ( pdata.count && !dryrun ) {
			ret = ext2fs_write_inode(current_fs, inum, &inode) ;
			if ( ret ) {
				fprintf(stderr, "%s: failed to write inode data %s\n", argv[0],
						argv[i]) ;
				continue ;
			}

			ext2fs_mark_bb_dirty(current_fs) ;
			ext2fs_mark_super_dirty(current_fs) ;
		}

		if ( verbose ) {
			printf("\r%d/%d/%d %s\n", pdata.count, pdata.blocks,
					pdata.total_blocks, argv[i]);
		}
	}

	ret = ext2fs_close(current_fs) ;
	if ( ret ) {
		fprintf(stderr, "%s: error while closing filesystem\n", argv[0]) ;
		return 1 ;
	}

	return 0 ;
}
