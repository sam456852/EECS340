#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


#include <iostream>

#include "Minet.h"
#include "sockint.h"
#include "tcpstate.h"

const unsigned  int DEFAULT_TIMERTRIES = 5;
const double TIME_OUT = 10;

using std::cout;
using std::endl;
using std::cerr;
using std::string;

unsigned int initSeq(){    
  int randNum = 0;  
    int fd = open("/dev/urandom", O_RDONLY);  
    if (-1 == fd) {  
      printf("error\n");  
      return 1;  
    }  
    read(fd, (char *)&randNum, sizeof(unsigned int));  
    close(fd);
    return randNum;  
}

// last_sent: next send seq = last_sent+1
// last_acked: the acknum of the last packet received - 1

Packet packetBuilder(ConnectionToStateMapping<TCPState> &cs, unsigned char flags, Buffer data, bool isResent = false){
  
  cerr << "Building Packet!--------------------------------------------------" << endl;
  Packet p;
  unsigned int datalen = data.GetSize();
  if (datalen != 0) {
    p = Packet(data);
  }
  IPHeader iph;
  TCPHeader tcph;

  iph.SetProtocol(cs.connection.protocol);
  iph.SetSourceIP(cs.connection.src);
  iph.SetDestIP(cs.connection.dest);
  iph.SetTotalLength(datalen + IP_HEADER_BASE_LENGTH +TCP_HEADER_BASE_LENGTH);
  p.PushFrontHeader(iph);
  cout << "IPHeader Set!" << endl;
   
  tcph.SetSourcePort(cs.connection.srcport, p);
  tcph.SetDestPort(cs.connection.destport, p);
  if (isResent) {
    tcph.SetSeqNum(cs.state.last_acked+1, p);
  } else {
    tcph.SetSeqNum(cs.state.last_sent+1, p);
  }
  tcph.SetAckNum(cs.state.last_recvd+1, p);
  tcph.SetHeaderLen(TCP_HEADER_BASE_LENGTH/4, p);
  tcph.SetFlags(flags, p);
  tcph.SetWinSize(cs.state.GetRwnd(), p);
  tcph.SetChecksum(0);
  tcph.SetUrgentPtr(0, p);
  p.PushBackHeader(tcph);
  cout << "TCPHeader Set!" << endl;

  cerr << "Completed Building!--------------------------------------------------" << endl;
  cerr << "TCP Packet: IP Header is " << iph << endl;
  cerr << "TCP Header is " << tcph << endl;
  cerr << "Checksum is " << (tcph.IsCorrectChecksum(p) ? "VALID" : "INVALID") << endl;
  
  return p;
}

int main(int argc, char *argv[])
{
  MinetHandle mux, sock;
 
  ConnectionList<TCPState> clist;

  MinetInit(MINET_TCP_MODULE);

  mux=MinetIsModuleInConfig(MINET_IP_MUX) ? MinetConnect(MINET_IP_MUX) : MINET_NOHANDLE;
  sock=MinetIsModuleInConfig(MINET_SOCK_MODULE) ? MinetAccept(MINET_SOCK_MODULE) : MINET_NOHANDLE;

  //unsigned int Seq = initSeq();
  //cout<<Seq<<endl;

  if (MinetIsModuleInConfig(MINET_IP_MUX) && mux==MINET_NOHANDLE) {
    MinetSendToMonitor(MinetMonitoringEvent("Can't connect to mux"));
    return -1;
  }

  if (MinetIsModuleInConfig(MINET_SOCK_MODULE) && sock==MINET_NOHANDLE) {
    MinetSendToMonitor(MinetMonitoringEvent("Can't accept from sock module"));
    return -1;
  }

  MinetSendToMonitor(MinetMonitoringEvent("tcp_module handling TCP traffic"));

  MinetEvent event;
  double timeout = 1; // second

  while (MinetGetNextEvent(event, timeout)==0) {
  // if we received an unexpected type of event, print error
  if ((event.eventtype!=MinetEvent::Dataflow || event.direction!=MinetEvent::IN) && event.eventtype != MinetEvent::Timeout) {
    MinetSendToMonitor(MinetMonitoringEvent("Unknown event ignored."));
    cout << "Unknown event ignored." << endl;
  // if we received a valid event from Minet, do processing
  } else {
    cerr << "invalid event from Minet" << endl;
  
  // if there is a time out event
  if (event.eventtype == MinetEvent::Timeout) {
    ConnectionList<TCPState>::iterator ctime = clist.FindEarliest();
    cout << "Earliest connection:--------------------" << (*ctime).timeout << endl;
    if (ctime != clist.end() && difftime(Time(), (*ctime).timeout) >= TIME_OUT) {
      // if there is a time out
      cout << "different time: " << difftime(Time(), (*ctime).timeout) << endl;
      cout << "last_acked:    " << (*ctime).state.last_acked << endl;
      cout << "last_sent:     " << (*ctime).state.last_sent << endl;
      switch ((*ctime).state.GetState()) {
	case SYN_SENT:
	{
	  if ((*ctime).state.tmrTries >= 0) {
	    Packet pReSYN = packetBuilder((*ctime), (unsigned char)0x02, Buffer(), true);
	    MinetSend(mux,pReSYN);
	  } else {
	    cout << "Can not connect to the host" << endl;
	    (*ctime).state.SetState(CLOSED);
            cout<<"Connection CLOSED!"<<endl;
	  }
	  
	  (*ctime).state.tmrTries = (*ctime).state.tmrTries - 1;
	  (*ctime).timeout.SetToCurrentTime();
	}
	break;
	
	case ESTABLISHED:
	{
	  if ((*ctime).state.last_sent - (*ctime).state.last_acked > 0) {
	    cout << "Start resend----------------------------------------------------" << endl;
	    (*ctime).state.SetLastSent((*ctime).state.last_acked);
            unsigned bytes = (*ctime).state.SendBuffer.GetSize()-((*ctime).state.last_sent - (*ctime).state.last_acked);
	    unsigned offsetlastsent;
	    size_t bytesize = 1;
	    while (bytes != 0 && bytesize != 0) {
	      (*ctime).state.SendPacketPayload(offsetlastsent, bytesize, bytes);
	      //cout << "ready to get load" << endl;
	      //(*cs).state.SendBuffer.GetData(load, bytesize, offsetlastsent);
	      unsigned char flags = 0x10;   //ACK=1
	      Buffer b = (*ctime).state.SendBuffer.Extract(offsetlastsent, bytesize);
	      //cout << "SendBuffer get:  " << b << endl;
              Packet pSend = packetBuilder((*ctime), flags, b);
	      MinetSend(mux, pSend);
	      (*ctime).state.SendBuffer.Insert(b, offsetlastsent);
	      //cout << "SendBuffer now is: " << (*ctime).state.SendBuffer << endl;
	      (*ctime).state.SetLastSent((*ctime).state.last_sent + bytesize);
	      bytes = bytes - bytesize;
	      (*ctime).timeout.SetToCurrentTime();
	    }
	    //(*ctime).state.SetLastSent((*ctime).state.last_acked + datasize);
	    //(*ctime).timeout.SetToCurrentTime();
	  }
	}
	break;
	
	case LAST_ACK:
	{
	  unsigned char flags = 0x11; // ACK = 1 FIN = 1
          Packet pReFIN = packetBuilder((*ctime), flags, Buffer(), true);
          MinetSend(mux, pReFIN);
	  (*ctime).timeout.SetToCurrentTime();
	}
	break;
	
	case TIME_WAIT:
	{
	  cout << "Last State is TIME_WAIT--------------------------------------------" << endl;
	  if (difftime(Time(), (*ctime).timeout) >= 2*MSL_TIME_SECS) {
	    (*ctime).state.SetState(CLOSED);
            cout<<"Connection CLOSED!"<<endl;
	  }
	}
	break;
	
	case CLOSE_WAIT:
	{
	  cout<<"Last State is CLOSE_WAIT-------------------------------------" << endl;
	  if ((*ctime).state.last_sent - (*ctime).state.last_acked > 0) {
	    cout << "Start resend" << endl;
	    (*ctime).state.SetLastSent((*ctime).state.last_acked);
            unsigned bytes = (*ctime).state.SendBuffer.GetSize()-((*ctime).state.last_sent - (*ctime).state.last_acked);
	    unsigned offsetlastsent;
	    size_t bytesize = 1;
	    while (bytes != 0 && bytesize != 0) {
	      (*ctime).state.SendPacketPayload(offsetlastsent, bytesize, bytes);
	      //cout << "ready to get load" << endl;
	      //(*cs).state.SendBuffer.GetData(load, bytesize, offsetlastsent);
	      unsigned char flags = 0x10;   //ACK=1
	      Buffer b = (*ctime).state.SendBuffer.Extract(offsetlastsent, bytesize);
	      cout << "SendBuffer get:  " << b << endl;
              Packet pSend = packetBuilder((*ctime), flags, b);
	      MinetSend(mux, pSend);
	      (*ctime).state.SendBuffer.Insert(b, offsetlastsent);
	      cout << "SendBuffer now is: " << (*ctime).state.SendBuffer << endl;
	      (*ctime).state.SetLastSent((*ctime).state.last_sent + bytesize);
	      bytes = bytes - bytesize;
	      (*ctime).timeout.SetToCurrentTime();
	    }
	    //(*ctime).state.SetLastSent((*ctime).state.last_acked + datasize);
	    //(*ctime).timeout.SetToCurrentTime();
	  }
	}
	break;
	
	default:
	{
	}
      }
    }
  }
  
  //  Data from the IP layer below  //
  if (event.handle==mux) {
    Packet p;

    MinetReceive(mux,p);

    unsigned tcphlen=TCPHeader::EstimateTCPHeaderLength(p);
    cerr << "estimated header len = " << tcphlen << endl;
    p.ExtractHeaderFromPayload<TCPHeader>(tcphlen);
    IPHeader ipl = p.FindHeader(Headers::IPHeader);
    TCPHeader tcph = p.FindHeader(Headers::TCPHeader);
    bool checksumok = tcph.IsCorrectChecksum(p);
    
    cerr << "TCP Received Packet---------------------------------------------" << endl;
    cerr << "TCP Packet: IP Header is " << ipl << " and ";
    cerr << "TCP Header is " << tcph << " and ";
    cerr << "Checksum is " << (tcph.IsCorrectChecksum(p) ? "VALID" : "INVALID") << endl;
    
    cerr << "Packet!!!!Payload--------------------" << endl;
    cerr << p.GetPayload() << endl;

    Connection c;
    

    ipl.GetDestIP(c.src);
    ipl.GetSourceIP(c.dest);
    ipl.GetProtocol(c.protocol);
    tcph.GetDestPort(c.srcport);
    tcph.GetSourcePort(c.destport);
    ConnectionList<TCPState>::iterator cs = clist.FindMatching(c);

    if (cs != clist.end()) {
      unsigned int ackNum;
      unsigned int seqNum;
      unsigned char flags;
      unsigned short rwnd;
      tcph.GetAckNum(ackNum);
      tcph.GetSeqNum(seqNum);
      tcph.GetFlags(flags);
      tcph.GetWinSize(rwnd);
      unsigned short ipTotalLen;
      //unsigned int ipHeaderLen = IPHeader::EstimateIPHeaderLength(p);
      ipl.GetTotalLength(ipTotalLen);
      //unsigned int datastart = ipTotalLen - ipHeaderLen - tcphlen;
      //cerr << "datastart: " << datastart << endl; 

      Buffer &data = p.GetPayload();
      //Buffer &data = p.GetPayload().ExtractFront(datastart);
      /*Buffer data;
      if(datastart > 0){
      char *temp = new char[datastart];
      p.GetPayload().GetData(temp, datastart, 0);
      data = *(new Buffer(temp, datastart));
      }*/
      
      // get the real string length
      unsigned int datalen = 0;
      char* tmp = new char[data.GetSize()];
      data.GetData(tmp, data.GetSize(), 0);
      cout << tmp << endl;
      datalen = data.GetSize();
      while (datalen >= 1 && tmp[datalen-1] == 0x00) {
	datalen --;
      }
      
      if (rwnd != 0) {
        (*cs).state.SetSendRwnd(rwnd);     // flow control
      } else {
        (*cs).state.SetSendRwnd(1);
      }

      cout << "A new packet data come----------------------------------" << endl;
      cout << data << endl;    
      //unsigned int datalen = data.GetSize();

      //ConnectionToStateMapping<TCPState> currentCon = *cs;

      switch ((*cs).state.GetState()) {
        case CLOSED:
        {
	  cout<<"Last State is CLOSED!-------------------------------------"<<endl;
        }
        break;
        case LISTEN:
        {
          cout << "Last State is LISTEN!------------------------------------" << endl;
          if (IS_SYN(flags) /*&& currentCon.state.SetLastRecvd(seqNum, datalen)*/) {
            cout << "IS SYN" << endl;//passive open
            (*cs).state.SetLastRecvd(seqNum);
            (*cs).connection = c;
            Packet p = packetBuilder((*cs), (unsigned char)18, Buffer());
            MinetSend(mux, p);
            (*cs).state.SetLastSent((*cs).state.last_sent + 1);
            (*cs).state.SetState(SYN_RCVD);
            (*cs).timeout.SetToCurrentTime();
            //MinetSend(sock, SockRequestResponse(WRITE, c, Buffer(), 0, EOK));
           
          } else {
            cout << "IS Listening!" << endl;
            TCPState tcpState(initSeq(), CLOSED, DEFAULT_TIMERTRIES);
            ConnectionToStateMapping<TCPState> m (c, Time(), tcpState, false);
            m.state.SetLastRecvd(seqNum + 1); // ?
            Packet p = packetBuilder(m, (unsigned char)20, Buffer());
            MinetSend(mux, p);
          }
        }
        break;
        case SYN_RCVD:
        {
          cout << "Last State is SYN_RCVD!------------------------------------" << endl;
          if (IS_RST(flags) /*&& currentCon.state.SetLastRecvd(seqNum, datalen)*/) {
            cout << "IS RST!" << endl;
            TCPState tcpReset(initSeq(), LISTEN, DEFAULT_TIMERTRIES);
            ConnectionToStateMapping<TCPState> rstCon(c, Time(), tcpReset, true);
            clist.erase(cs);
            clist.push_back(rstCon);
            cout << "Reset Connection!------------------------------------" << endl;
          } else if (IS_ACK(flags) /*&& (*cs).state.last_sent == ackNum - 1*/ && (*cs).state.SetLastRecvd(seqNum, datalen)) {
            cout<<"IS ACK"<<endl;      
	    
            cout << "seqNum:" << seqNum << endl;
            cout << "LastRcvd:" << (*cs).state.last_recvd << endl;
            (*cs).state.SetLastRecvd(seqNum-1);      // for the establish first receive packet
            cout << "LastRcvd:" << (*cs).state.last_recvd << endl;    

            (*cs).connection = c;
            (*cs).state.SetLastAcked(ackNum);
            (*cs).state.SetState(ESTABLISHED);
            (*cs).bTmrActive = true;
	    (*cs).timeout.SetToCurrentTime();

            cout << "Connection Established!------------------------------------" << endl;
	    cout << "Connection: " << (*cs).connection << endl;
            //cout << "ACK packet" << p << endl;
            //cout << "datalen" << data.GetSize() << endl;
            //cout << "datastart" << datastart << endl;
            //cout << "data" << data << endl;
          
            /*if (data.GetSize() > 0) {
              MinetSend(sock, SockRequestResponse(WRITE, c, data, data.GetSize(), EOK));
              cout << "Data Sent to Socket!" << endl;
            }*/
	    MinetSend(sock, SockRequestResponse(WRITE, (*cs).connection, Buffer(), 0, EOK));
          }
        }
        break;
        case SYN_SENT:
        {
	  cout<<"Last State is SYN_SENT!------------------------------------"<<endl;
	  if (IS_ACK(flags) && IS_SYN(flags) && (*cs).state.SetLastAcked(ackNum)) {
	    cout << "Received the SYN,ACK" << endl;
	    (*cs).state.SetLastRecvd(seqNum);
	    MinetSend(mux, packetBuilder((*cs), (unsigned char)0x10, Buffer()));
	    //currentCon.state.SetLastSent(currentCon.state.last_sent+1);
	    (*cs).state.SetState(ESTABLISHED);
	    (*cs).timeout.SetToCurrentTime();
	    //(*cs).state.SetLastAcked(ackNum);
	    MinetSend(sock, SockRequestResponse(WRITE, (*cs).connection, Buffer(), 0, EOK));
	    cout << "Established!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << endl;
	  }
	
        /*if(IS_ACK(flags) && currentCon.state.last_sent == ackNum){
          cout<<"IS ACK"<<endl;
          currentCon.connection = c;
          currentCon.state.SetLastAcked(ackNum);
          currentCon.state.SetState(SYN_SENT1);
          currentCon.bTmrActive = false;
          clist.erase(cs);ConnectionToStateMapping<TCPState> newConn;
        newConn.connection = req.connection;
          clist.push_back(currentCon);
        }else if(IS_SYN(flags) && currentCon.state.SetLastRecvd(seqNum, datalen)){
          cout<<"IS SYN"<<endl;
          MinetSend(mux, packetBuilder    (currentCon, (unsigned char)16, Buffer()));
          currentCon.state.SetState(ESTABLISHED);
          currentCon.timeout.SetToCurrentTime();
          MinetSend(sock, SockRequestResponse(WRITE, c, Buffer(), 0, EOK));
          clist.erase(cs);
          clist.push_back(currentCon);
          cout<<"Passive Open Established!"<<endl;
        }else if(IS_RST(flags) && currentCon.state.last_sent == ackNum){
          cout<<"IS RST!"<<endl;
          TCPState tcpReset(initSeq(), LISTEN, DEFAULT_TIMERTRIES);
          ConnectionToStateMapping<TCPState> rstCon(c, Time(), tcpReset, false);
          clist.erase(cs);
          clist.push_back(rstCon);
          cout<<"Reset Connection!"<<endl;
        }else{
          cout<<"Half-Open Connection Discovery"<<endl;// not sure; by RFC 3.4 Figure 10
          TCPState tcpState(ackNum, SYN_SENT, DEFAULT_TIMERTRIES);
          ConnectionToStateMapping<TCPState> mConnectionToStateMapping<TCPState> newConn;
        newConn.connection = req.connection; (c, Time(), tcpState, false);
          Packet p = packetBuilder(m, (unsigned char)4, Buffer());
          MinetSend(mux, p);
        }*/
      }
      break;
      case SYN_SENT1:
      {
        cout<<"Last State is SYN_SENT1!------------------------------------"<<endl;
        if(IS_SYN(flags) && (*cs).state.SetLastRecvd(seqNum, datalen)){
          cout<<"IS SYN!"<<endl;
          MinetSend(mux, packetBuilder((*cs), (unsigned char)16, Buffer()));
          (*cs).state.SetState(ESTABLISHED);
          (*cs).timeout.SetToCurrentTime();
          MinetSend(sock, SockRequestResponse(WRITE, c, Buffer(), 0, EOK));
          cout<<"Active Open Established!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"<<endl;
        }

      }
      break;
        case ESTABLISHED:
        {
          cout << "Last State is ESTABLISHED!------------------------------------" << endl;
          if (IS_RST(flags)){
            cout<<"IS RST!"<<endl;
            TCPState tcpReset(initSeq(), CLOSED, DEFAULT_TIMERTRIES);
            ConnectionToStateMapping<TCPState> rstCon(c, Time(), tcpReset, true);
            clist.erase(cs);
            clist.push_back(rstCon);
            cout<<"Reset Connection!"<<endl;
          } else if (IS_FIN(flags)) {
            cout<<"IS FIN"<<endl;
	    (*cs).state.SetLastRecvd(seqNum);
            (*cs).connection = c;
            Packet p = packetBuilder((*cs), (unsigned char)0x10, Buffer());
            MinetSend(mux, p);
            //currentCon.state.SetLastSent(currentCon.state.last_sent + 1);
            (*cs).state.SetState(CLOSE_WAIT);
            (*cs).timeout.SetToCurrentTime();
            //MinetSend(sock, SockRequestResponse(CLOSE, c, Buffer(), 0, EOK));
	    MinetSend(sock, SockRequestResponse(WRITE, c, Buffer(), 0, EOK));
            cout<<"Enter CLOSE_WAIT!"<<endl;
          } else if (IS_ACK(flags) && (*cs).state.SetLastAcked(ackNum)) {
            //cout<<"sequence number?:"<<seqNum<<endl;
            //cout<<"LastRcvd:"<<(*cs).state.last_recvd<<endl;
            //cout<<"datalen:"<<datalen<<endl;
	    //cout<<"getsize"<<data.GetSize()<<endl;
      
            //cout<<"boolean?:"<<currentCon.state.SetLastRecvd(seqNum, datalen)<<endl;
	    // if receive a ACK and data, send ack back
	    if (datalen != 0) {
	      cout<<"IS ACK"<<endl;
	      if ((*cs).state.SetLastRecvd(seqNum, datalen)) {
	        (*cs).state.RecvBuffer.AddBack(data);
	      } else if ((*cs).state.SetLastRecvd(seqNum, MIN_MACRO((*cs).state.N, (*cs).state.TCP_BUFFER_SIZE-(*cs).state.RecvBuffer.GetSize()))) {
		Buffer &dataBuf = data.ExtractFront(MIN_MACRO((*cs).state.N, (*cs).state.TCP_BUFFER_SIZE-(*cs).state.RecvBuffer.GetSize()));
	        (*cs).state.RecvBuffer.AddBack(dataBuf);
	      }
	      cout << "Receive Buffer:  " << (*cs).state.RecvBuffer << endl;
              //(*cs).connection = c;
              Packet p = packetBuilder((*cs), (unsigned char)16, Buffer());
              MinetSend(mux, p);
              cout<<"ACK Sent!"<<endl;
	    } else {
	      // receive a ack, check if there something can be sent
	      if ((*cs).state.SendBuffer.GetSize() > ((*cs).state.last_sent - (*cs).state.last_acked)) {
		cout << "last_sent: " << (*cs).state.last_sent << endl;
		cout << "last_acked: "  << (*cs).state.last_acked << endl;
	        unsigned bytes = (*cs).state.SendBuffer.GetSize()-((*cs).state.last_sent - (*cs).state.last_acked);
		unsigned offsetlastsent;
		size_t bytesize = 1;
		while (bytes != 0 && bytesize != 0) {
		  (*cs).state.SendPacketPayload(offsetlastsent, bytesize, bytes);
		  //cout << "ready to get load" << endl;
		  //(*cs).state.SendBuffer.GetData(load, bytesize, offsetlastsent);
		  unsigned char flags = 0x10;   //ACK=1
		  Buffer b = (*cs).state.SendBuffer.Extract(offsetlastsent, bytesize);
		  cout << "SendBuffer get:  " << b << endl;
                  Packet pSend = packetBuilder((*cs), flags, b);
		  MinetSend(mux, pSend);
		  (*cs).state.SendBuffer.Insert(b, offsetlastsent);
		  cout << "SendBuffer now is: " << (*cs).state.SendBuffer << endl;
		  (*cs).state.SetLastSent((*cs).state.last_sent + bytesize);
		  bytes = bytes - bytesize;
		  (*cs).timeout.SetToCurrentTime();
		}
	      }
	    }
            //currentCon.state.SetLastSent(currentCon.state.last_sent + 1);
            (*cs).state.SetState(ESTABLISHED);
            (*cs).timeout.SetToCurrentTime();
	    Buffer &dataToSock = (*cs).state.RecvBuffer.ExtractFront((*cs).state.RecvBuffer.GetSize());
	    if (dataToSock.GetSize() != 0) {
	      cout << "Here is the dataTo Sock--------------------------------------------------------" << endl;
	      cout << dataToSock << endl;
	      MinetSend(sock, SockRequestResponse(WRITE, c, dataToSock, dataToSock.GetSize(), EOK));
	      cout<<"Data Sent to Socket!"<<endl;
	    }
	    //MinetSend(sock, SockRequestResponse(WRITE, c, data, datalen, EOK));
	    /*if (datalen == 0) {
	      MinetSend(sock, SockRequestResponse(WRITE, c, Buffer("error!",6), 6, EOK));
	    } else {
	      MinetSend(sock, SockRequestResponse(WRITE, c, data, datalen, EOK));
	    }*/
            //cout<<"Data Sent to Socket!"<<endl;
          }
        }
        break;
      case SEND_DATA: // seems useless
      {
	cout<<"Last State is SEND_DATA!------------------------------------"<<endl;

      }
      break;
      case CLOSE_WAIT: // seems useless
      {
	cout<<"Last State is CLOSE_WAIT!------------------------------------"<<endl;
	if (IS_ACK(flags) && (*cs).state.SetLastAcked(ackNum)) {
	    cout<<"Still got Acked!"<<endl;
            //cout<<"sequence number?:"<<seqNum<<endl;
            //cout<<"LastRcvd:"<<(*cs).state.last_recvd<<endl;
            //cout<<"datalen:"<<datalen<<endl;
	    //cout<<"getsize"<<data.GetSize()<<endl;
      
            //cout<<"boolean?:"<<currentCon.state.SetLastRecvd(seqNum, datalen)<<endl;
	    // if receive a ACK and data, send ack back
	    if (datalen != 0) {
	      cout<<"IS ACK"<<endl;
	      if ((*cs).state.SetLastRecvd(seqNum, datalen)) {
	        (*cs).state.RecvBuffer.AddBack(data);
	      } else if ((*cs).state.SetLastRecvd(seqNum, MIN_MACRO((*cs).state.N, (*cs).state.TCP_BUFFER_SIZE-(*cs).state.RecvBuffer.GetSize()))) {
		Buffer &dataBuf = data.ExtractFront(MIN_MACRO((*cs).state.N, (*cs).state.TCP_BUFFER_SIZE-(*cs).state.RecvBuffer.GetSize()));
	        (*cs).state.RecvBuffer.AddBack(dataBuf);
	      }
	      cout << "Receive Buffer:  " << (*cs).state.RecvBuffer << endl;
              //(*cs).connection = c;
              Packet p = packetBuilder((*cs), (unsigned char)16, Buffer());
              MinetSend(mux, p);
              cout<<"ACK Sent!"<<endl;
	    } else {
	      // receive a ack, check if there something can be sent
	      cout<<"Check Sendbuffer ready to send?"<<endl;
	      if ((*cs).state.SendBuffer.GetSize() > ((*cs).state.last_sent - (*cs).state.last_acked)) {
		cout << "last_sent: " << (*cs).state.last_sent << endl;
		cout << "last_acked: "  << (*cs).state.last_acked << endl;
	        unsigned bytes = (*cs).state.SendBuffer.GetSize()-((*cs).state.last_sent - (*cs).state.last_acked);
		unsigned offsetlastsent;
		size_t bytesize = 1;
		while (bytes != 0 && bytesize != 0) {
		  (*cs).state.SendPacketPayload(offsetlastsent, bytesize, bytes);
		  //cout << "ready to get load" << endl;
		  //(*cs).state.SendBuffer.GetData(load, bytesize, offsetlastsent);
		  unsigned char flags = 0x10;   //ACK=1
		  Buffer b = (*cs).state.SendBuffer.Extract(offsetlastsent, bytesize);
		  cout << "SendBuffer get:  " << b << endl;
                  Packet pSend = packetBuilder((*cs), flags, b);
		  MinetSend(mux, pSend);
		  (*cs).state.SendBuffer.Insert(b, offsetlastsent);
		  cout << "SendBuffer now is: " << (*cs).state.SendBuffer << endl;
		  (*cs).state.SetLastSent((*cs).state.last_sent + bytesize);
		  bytes = bytes - bytesize;
		  (*cs).timeout.SetToCurrentTime();
		}
	      }
	    }
          }
      }
      break;
      case FIN_WAIT1:
      {
        cout<<"Last State is FIN_WAIT1!------------------------------------"<<endl;
        if(IS_ACK(flags) && IS_FIN(flags) && (*cs).state.SetLastRecvd(seqNum, 1) && (*cs).state.SetLastAcked(ackNum)){
          cout<<"IS ACK and IS FIN"<<endl;
          //(*cs).connection = c;
          Packet p = packetBuilder((*cs), (unsigned char)16, Buffer());
          MinetSend(mux, p);
          cout<<"ACK Sent!"<<endl;
          (*cs).state.SetLastSent((*cs).state.last_sent + 1);
          (*cs).state.SetState(TIME_WAIT);
          (*cs).timeout.SetToCurrentTime();
          MinetSend(sock, SockRequestResponse(STATUS, c, Buffer(), 0, EOK));
          cout<<"Enter TIME_WAIT!------------------------------------"<<endl;
        }else if(IS_ACK(flags) && (*cs).state.SetLastAcked(ackNum)){
          cout<<"IS ACK"<<endl;
          //(*cs).connection = c;
          (*cs).state.SetLastRecvd(seqNum, datalen);//might come from SYN_RCVD
          (*cs).state.SetState(FIN_WAIT2);
          (*cs).timeout.SetToCurrentTime();
          cout<<"Enter FIN_WAIT2!------------------------------------"<<endl;
        }else if(IS_FIN(flags) && (*cs).state.SetLastAcked(ackNum)){
          (*cs).state.SetLastRecvd(seqNum, datalen); //might come from SYN_RCVD
          cout<<"IS FIN"<<endl;
          (*cs).connection = c;
          Packet p = packetBuilder((*cs), (unsigned char)16, Buffer());
          MinetSend(mux, p);
          cout<<"ACK Sent!"<<endl;
          (*cs).state.SetLastSent((*cs).state.last_sent + 1);
          (*cs).state.SetState(CLOSING);
          (*cs).timeout.SetToCurrentTime();
          MinetSend(sock, SockRequestResponse(WRITE, c, data, datalen, EOK));
          cout<<"Enter CLOSING!------------------------------------"<<endl;
        }

      }
      break;
      case CLOSING:
      {
        cout<<"Last State is CLOSING!------------------------------------"<<endl;
        if(IS_ACK(flags) && (*cs).state.SetLastAcked(ackNum)){
          cout<<"IS ACK"<<endl;
          //(*cs).connection = c;
          (*cs).state.SetLastRecvd(seqNum, datalen);//might come from SYN_RCVD
          (*cs).state.SetState(TIME_WAIT);
          (*cs).timeout.SetToCurrentTime();
          //clist.erase(cs);
          //clist.push_back(currentCon);
          cout<<"Enter TIME_WAIT!------------------------------------"<<endl;
	}
      }
      break;
        case LAST_ACK:
        {
          cout<<"Last State is LAST_ACK!------------------------------------"<<endl;
          if (IS_ACK(flags) && (*cs).state.last_sent + 1 == ackNum) {
            cout<<"FIN SENT IS ACK"<<endl;
            (*cs).state.SetState(CLOSED);
            cout<<"Connection CLOSED!------------------------------------"<<endl;
          }
	}
      break;
      case FIN_WAIT2:
      {
	cout<<"Last State is FIN_WAIT2!------------------------------------"<<endl;
        if(IS_FIN(flags) && (*cs).state.SetLastAcked(ackNum)){
          (*cs).state.SetLastRecvd(seqNum, datalen); //might come from SYN_RCVD
          cout<<"IS FIN"<<endl;
          //(*cs).connection = c;
          Packet p = packetBuilder((*cs), (unsigned char)16, Buffer());
          MinetSend(mux, p);
          cout<<"ACK Sent!"<<endl;
          (*cs).state.SetLastSent((*cs).state.last_sent + 1);
          (*cs).state.SetState(TIME_WAIT);
          (*cs).timeout.SetToCurrentTime();
          //MinetSend(sock, SockRequestResponse(WRITE, c, data, datalen, EOK));
	  MinetSend(sock, SockRequestResponse(STATUS, (*cs).connection, Buffer(), 0, EOK));
          cout<<"Enter TIME_WAIT!------------------------------------"<<endl;
        }
      }
      break;
      case TIME_WAIT://not sure about how to implement 2MSL timeout
      {
	
      }
      break;
      default:
      {
	
      }

    }

    if (!checksumok) {
    MinetSendToMonitor(MinetMonitoringEvent("forwarding packet to sock even though checksum failed"));
    }
  }else{
  MinetSendToMonitor(MinetMonitoringEvent("checksum failed!"));
  MinetSendToMonitor(MinetMonitoringEvent("Unknown port, sending ICMP error message"));
  IPAddress source; ipl.GetSourceIP(source);
  ICMPPacket error(source,DESTINATION_UNREACHABLE,PORT_UNREACHABLE,p);
  MinetSendToMonitor(MinetMonitoringEvent("ICMP error message has been sent to host"));
  MinetSend(mux, error);
  }

 


   
  }
      //  Data from the Sockets layer above  //
  if (event.handle==sock) {
        SockRequestResponse req;
        MinetReceive(sock,req);
	cerr << "Received Socket Request:" << req << endl;
        switch (req.type) {
        case CONNECT:
          { // active open to remote     
	cout << "APP Ask to CONNECT--------------------------" << endl;
        TCPState tcpSynSent(initSeq(), SYN_SENT, DEFAULT_TIMERTRIES);
	//tcpSynSent.N = 0;
	//pSynSent.SetLastRecvd(-1);
	ConnectionToStateMapping<TCPState> newConn(req.connection, Time(), tcpSynSent, true);
        ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
        if (cs != clist.end()) {
	  clist.erase(cs);
	}
	Packet pSYN = packetBuilder(newConn, (unsigned char)0x02, Buffer());
	MinetSend(mux,pSYN);
	//sleep(5);
	//MinetSend(mux,pSYN);
	newConn.state.SetLastSent(newConn.state.last_sent+1);
	clist.push_back(newConn);
        // return STATUS to sock
        MinetSend(sock, SockRequestResponse(STATUS, req.connection, Buffer(), 0, EOK));
	cout << "CONNECT SYN Sent!--------------------------" << endl;
          }
          break;
          case ACCEPT:
          { // passive open from remote
            TCPState tcpListen(initSeq(), LISTEN, DEFAULT_TIMERTRIES);
            ConnectionToStateMapping<TCPState> newListen(req.connection, Time(), tcpListen, true);
	    ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            if (cs != clist.end()) {
	      clist.erase(cs);
	    }
            clist.push_back(newListen);
            // return STATUS to sock
            SockRequestResponse repl;
            repl.type=STATUS;
            repl.error=EOK;
            MinetSend(sock,repl);
            //cout << "Received Socket Request:" << req << endl;
          }
          break;
          case WRITE:
          {
	    cout << "Sock ask to write!------------------------------------" << endl; 
            ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            if(cs != clist.end() && ((*cs).state.GetState() == ESTABLISHED || (*cs).state.GetState() == CLOSE_WAIT) ) {
              // may be check the size of the buffer
              // put data into Sendbuffer
	      unsigned datatobuffer = 0;
	      unsigned reqsize = req.data.GetSize();
              if ((*cs).state.last_sent - (*cs).state.last_acked < (*cs).state.TCP_BUFFER_SIZE) {
                if ((*cs).state.last_sent - (*cs).state.last_acked + req.data.GetSize() <= (*cs).state.TCP_BUFFER_SIZE) {
		  (*cs).state.SendBuffer.AddBack(req.data);
		  datatobuffer = req.data.GetSize();
		} else {
		  //char* tobuffer;
		  datatobuffer = ((*cs).state.TCP_BUFFER_SIZE - ((*cs).state.last_sent - (*cs).state.last_acked));
		  Buffer b = req.data.ExtractFront(datatobuffer);
		  (*cs).state.SendBuffer.AddBack(b);
		}
		//cout << "AddBack" << endl;
		//cout << "last_acked:   " << (*cs).state.last_acked << endl;
		//cout << "last_sent:    " << (*cs).state.last_sent << endl;
		
		// make packets and put them into queue
		unsigned bytes = (*cs).state.SendBuffer.GetSize()-((*cs).state.last_sent - (*cs).state.last_acked);
		unsigned offsetlastsent;
		size_t bytesize = 1;
		while (bytes != 0 && bytesize != 0) {
		  (*cs).state.SendPacketPayload(offsetlastsent, bytesize, bytes);
		  //cout << "ready to get load" << endl;
		  //(*cs).state.SendBuffer.GetData(load, bytesize, offsetlastsent);
		  cout << "offsetlastsent: " << offsetlastsent << endl;
		  cout << "bytesize: " << bytesize << endl;
		  unsigned char flags = 0x10;   //ACK=1
		  Buffer b = (*cs).state.SendBuffer.Extract(offsetlastsent, bytesize);
		  cout << "SendBuffer get:  " << b << endl;
                  Packet pSend = packetBuilder((*cs), flags, b);
		  MinetSend(mux, pSend);
		  (*cs).state.SendBuffer.Insert(b, offsetlastsent);
		  cout << "SendBuffer now is: " << (*cs).state.SendBuffer << endl;
		  (*cs).state.SetLastSent((*cs).state.last_sent + bytesize);
		  bytes = bytes - bytesize;
		  (*cs).timeout.SetToCurrentTime();
		}
                // if there is data in the window [sssssssssdddddddd], get the data that should be sent
                /*if ((*cs).state.N >= ((*cs).state.last_sent - (*cs).state.last_acked)) {
		  cout << "Start send" << endl;
                  char* datasend;
		  size_t datasendsize = MIN_MACRO(req.data.GetSize(), (*cs).state.N - ((*cs).state.last_sent - (*cs).state.last_acked));
                  size_t datasize = (*cs).state.SendBuffer.GetData(datasend, datasendsize, ((*cs).state.last_sent - (*cs).state.last_acked));
                  //unsigned ds = (*cs).state.SendBuffer.GetData(datasend, MIN_MACRO(req.data.GetSize(), (*cs).state.N - ((*cs).state.last_sent - (*cs).state.last_acked)), ((*cs).state.last_sent - (*cs).state.last_acked));
		  unsigned char flags = 0x10;   //ACK=1
                  Packet pSend = packetBuilder((*cs), flags, Buffer(datasend, datasize));
		  //Packet pSend = packetBuilder((*cs), flags, req.data);
		  cout << "Send Packet:------------------------------------------------" << endl;
		  cout << pSend << endl;
                  MinetSend(mux, pSend);
		  (*cs).state.SetLastSent((*cs).state.last_sent + datasize);
		  (*cs).timeout.SetToCurrentTime();
		  
		  //(*cs).state.SetLastSent((*cs).state.last_sent + req.data.GetSize());
                  // return STATUS to sock
                  //MinetSend(sock,SockRequestResponse(STATUS, (*cs).connection, Buffer(), ds, EOK));
	          //MinetSend(sock, SockRequestResponse(STATUS, (*cs).connection, Buffer(), (unsigned)datasize, EOK));
                }*/
              }
              //if (datatobuffer < reqsize) {
	      //  MinetSend(sock, SockRequestResponse(STATUS, (*cs).connection, Buffer(), datatobuffer, EBUF_SPACE));
	      //} else {
	        MinetSend(sock, SockRequestResponse(STATUS, (*cs).connection, Buffer(), datatobuffer, EOK));
	      //}
            }
          }
          break;
        case FORWARD:
        {
           SockRequestResponse repl;
           repl.type = STATUS;
           repl.error = EOK;
           MinetSend(sock, repl);
        }
        break;
        case CLOSE:
        {
	   cout << "APP Ask to close--------------------------" << endl;
           ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
           if (cs != clist.end()){
	     if ((*cs).state.GetState() == CLOSE_WAIT) {
	       unsigned char flags = 0x11; // ACK = 1 FIN = 1
               Packet pFin = packetBuilder((*cs), flags, Buffer());
               MinetSend(mux, pFin);
	       (*cs).state.SetLastSent((*cs).state.last_sent + 1);
               (*cs).state.SetState(LAST_ACK);
               MinetSend(sock, SockRequestResponse(STATUS, (*cs).connection, Buffer(), 0, EOK));
	     } else if ((*cs).state.GetState() == ESTABLISHED) {
	       unsigned char flags = 0x11; // ACK = 1 FIN = 1
               Packet pFin = packetBuilder((*cs), flags, Buffer());
               MinetSend(mux, pFin);
	       (*cs).state.SetLastSent((*cs).state.last_sent + 1);
               (*cs).state.SetState(FIN_WAIT1);
               MinetSend(sock, SockRequestResponse(STATUS, (*cs).connection, Buffer(), 0, EOK));
	     }
           }
        }
        break;
        case STATUS:
        {
	  cout << "From Sock is STATUS!------------------------------------" << endl;
	  /*if (req.error == EINVALID_OP && req.bytes == 0) {
	    ConnectionList<TCPState>::iterator cs = clist.FindMatching(req.connection);
            if (cs != clist.end()) {
	      unsigned char flags = 0x11; // ACK = 1 FIN = 1
              Packet pFin = packetBuilder((*cs), flags, Buffer());
              MinetSend(mux, pFin);
	      (*cs).state.SetLastSent((*cs).state.last_sent + 1);
              (*cs).state.SetState(LAST_ACK);
              MinetSend(sock, SockRequestResponse(STATUS, (*cs).connection, Buffer(), 0, EOK));
	      cout << "Goto the LAST_ACK" << endl;
	    }
	  }*/
        }
        break;
        default:
      {
        SockRequestResponse repl;
        repl.type=STATUS;
        repl.error=EWHAT;
        MinetSend(sock,repl);
      }
      }
    //cerr << "Received Socket Request:" << req << endl;
  }
  }
  }
  return 0;
}
