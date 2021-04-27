CC = gcc -w 
SRCSERVER = .
SRCCLIENT = .
BINSERVERFORKED = bin/preforked-FTPserver
BINSERVERTHREAD = bin/prethread-FTPserver
BINCLIENT = bin/FTPClient
OBJCLIENT = obj
OBJSERVER = obj
OBJECTSCLIENT = ${OBJCLIENT}/commons.o ${OBJCLIENT}/ftpclient.o
OBJECTSSERVERTHREAD = ${OBJSERVER}/commons.o ${OBJSERVER}/prethread-FTPserver.o
EXECUTABLESERVERTHREAD = ${BINSERVERTHREAD}/prethread-FTPserver.out
OBJECTSSERVERFORKED = ${OBJSERVER}/commons.o ${OBJSERVER}/preforked-FTPserver.o
EXECUTABLESERVERFORKED = ${BINSERVERFORKED}/preforked-FTPserver.out
EXECUTABLECLIENT = ${BINCLIENT}/ftpclient.out

all:	clean client fork thread

client:	${OBJECTSCLIENT}
	${CC} $^ -o ${EXECUTABLECLIENT}

thread:	${OBJECTSSERVERTHREAD}
	${CC} $^ -o ${EXECUTABLESERVERTHREAD} -pthread

fork:	${OBJECTSSERVERFORKED}
	${CC} $^ -o ${EXECUTABLESERVERFORKED} --std=c11

${OBJSERVER}/%.o:	${SRCSERVER}/%.c
	${CC} -c $< -o $@

${OBJCLIENT}/%.o:	${SRCCLIENT}/%.c
	${CC} -c $< -o $@


clean:
	rm -f ${OBJSERVER}/*.o
	rm -f ${BINSERVER}/*.out
	rm -f ${OBJCLIENT}/*.o
	rm -f ${BINCLIENT}/*.out
