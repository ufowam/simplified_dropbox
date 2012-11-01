simplified_dropbox
==================

The fourth assignment of my "Software Tools and Systems Programming" class I took in the fall 2012. It is a simplified version of the dropbox tool: the client connects to the server specifying a certain directory. This directory is then recursively copied and synced on the server.

How to use dbclient:
- Usage: ./dbclient host userid directory
where host is the server's hostname, userid the username which will 
identify you to the server and directory the directoy the you would like 
to share.
- If you share a directory that is already being used by another client 
on the server, then this directory will be shared between you and this 
client.
- If you disconnet from the server, any information related to you and 
your directory will be lost and you will have to re-syncronize your 
directory next time you connect to the server

How to use dbserver:
- Just type ./dbserver in a terminal
- dbserver stores the client's files in a directory called clientdir 
containing all the folder's synchronized by clients. 
- dbserver does not support deletion of files.

To do:
- Replace the static array limiting the server to 10 clients by a dynamic array
- Add delete functionalities
