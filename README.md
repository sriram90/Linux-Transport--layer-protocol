# Linux-Transport--layer-protocol
A new Transport layer protocol in Linux kernel 

CSE5361 contains a character device driver module which initializes a new transport layer protocol on top of existing IP Layer.
The New Transport layer protocol sends and receives packet to other machines using write() call and read() call of the character 
device interface.
Compile this character device module and install it into the linux kernel.

Use the CSE5361app application to test this new transport layer protocol
