# RDMA-example-application
Writing RDMA applications on Linux Example programs  by Roland Dreier.

source: http://www.digitalvampire.org/rdma-tutorial-2007/notes.pdf

#use

please make sure you have  rdma  and libibverbs  library.

##compiler
client :

$ cc -o client client.c -lrdmacm -libverbs

server :

$ cc -o server server.c -lrdmacm 


##run
server :

$ ./server

client : ( syntax:  client servername val1 val2)

$./client  192.168.1.2  123 567 

123 + 567 = 690

