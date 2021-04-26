CC = gcc -w 
SRCSERVER = .
SRCCLIENT = .
BINSERVER = bin/prethread-FTPserver
BINCLIENT = bin/Client
OBJCLIENT = obj
OBJSERVER = obj
OBJECTSCLIENT = ${OBJCLIENT}/commons.o ${OBJCLIENT}/client.o
OBJECTSSERVER = ${OBJSERVER}/commons.o ${OBJSERVER}/server.o
EXECUTABLESERVER = ${BINSERVER}/server.out
EXECUTABLECLIENT = ${BINCLIENT}/client.out

all:	clean client server

client:	${OBJECTSCLIENT}
	${CC} $^ -o ${EXECUTABLECLIENT}

server:	${OBJECTSSERVER}
	${CC} $^ -o ${EXECUTABLESERVER} -pthread 

${OBJSERVER}/%.o:	${SRCSERVER}/%.c
	${CC} -c $< -o $@

${OBJCLIENT}/%.o:	${SRCCLIENT}/%.c
	${CC} -c $< -o $@


clean:
	rm -f ${OBJSERVER}/*.o
	rm -f ${BINSERVER}/*.out
	rm -f ${OBJCLIENT}/*.o
	rm -f ${BINCLIENT}/*.out
