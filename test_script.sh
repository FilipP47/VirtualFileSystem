#!/bin/bash

set -e

VFS_NAME="my_virtual_disk.vfs"
VFS_SIZE=409600
BLOCK_SIZE=512

echo "Creating Virtual Disk '$VFS_NAME' with size $VFS_SIZE bytes and block size $BLOCK_SIZE bytes"
./fs_util create $VFS_NAME $VFS_SIZE $BLOCK_SIZE
echo ""


echo "Listing Directory (Expected: No files)"
echo ""
./fs_util $VFS_NAME ls
echo ""
echo "Directory listed."
echo ""


create_sample_files() {
    echo "Creating sample files test1.txt, test2.txt, test3.txt"
    echo ""
    ./fs_util $VFS_NAME add test1.txt $BLOCK_SIZE
    ./fs_util $VFS_NAME add test2.txt $BLOCK_SIZE
    ./fs_util $VFS_NAME add test3.txt $(expr $BLOCK_SIZE \* 2)
    ./fs_util $VFS_NAME add test4.txt $BLOCK_SIZE
    echo ""
    echo "Sample files created."
    echo ""
}

create_sample_files




induce_fragmentation() {
    echo "Inducing fragmentation by deleting some files"
    echo ""
    ./fs_util $VFS_NAME mem
    echo ""
    ./fs_util $VFS_NAME rm test1.txt
    ./fs_util $VFS_NAME rm test3.txt
    echo ""
    echo "Selected files removed to create fragmented free blocks."
    echo ""
    ./fs_util $VFS_NAME mem
    echo ""
    echo "We can see free blocks in memory between the used blocks."
    echo ""
    ./fs_util $VFS_NAME ls
    echo ""
    echo "Two files left."
    echo ""
    ./fs_util $VFS_NAME add test5.txt $(expr $BLOCK_SIZE \* 5)
    echo ""
    ./fs_util $VFS_NAME mem
    echo ""
    echo "Added a file of size 5 blocks. As we can see, it is fragmented."
    echo ""
}

induce_fragmentation


show_defragmentation() {
    echo "Defragmentation Test"
    echo "Memory before defragmentation"
    echo ""
    ./fs_util $VFS_NAME mem
    echo ""
    echo "Before defragmentation let's delete some files"
    echo ""
    ./fs_util $VFS_NAME rm test2.txt
    echo ""
    ./fs_util $VFS_NAME mem
    echo ""
    echo "Defragmenting the virtual disk"
    echo ""
    ./fs_util $VFS_NAME defrag
    echo ""
    echo "Defragmentation completed."
    echo ""
    ./fs_util $VFS_NAME mem
    echo ""
    echo "Memory is now contiguous."
    echo "Delete some files to check defragmentation"
    echo ""
    ./fs_util $VFS_NAME rm test5.txt
    echo ""
    echo "Memory after deleting a file"
    echo ""
    ./fs_util $VFS_NAME mem
    echo ""
    echo "As we see file test5.txt was contiguous."
    echo ""
    ./fs_util $VFS_NAME ls
    echo ""
}

show_defragmentation

./fs_util $VFS_NAME die
echo "All tests completed successfully."