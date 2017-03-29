#include "node.h"
#include "context.h"
#include "error.h"
#include "topology.h"

Node::Node(const unsigned n, SimulationContext *c, double b, double l) : 
    number(n), context(c), bw(b), lat(l) 
{
  
  #if defined(LINKSTATE)
    NodeTable = Table(n);
  #endif
  
  
  #if defined(DISTANCEVECTOR)
  cout<<"DV table built!"<<endl;
  nodeTable = new Table(number);
  #endif
}

Node::Node() 
{ throw GeneralException(); }

Node::Node(const Node &rhs) : 
  number(rhs.number), context(rhs.context), bw(rhs.bw), lat(rhs.lat) {}

Node & Node::operator=(const Node &rhs) 
{
  return *(new(this)Node(rhs));
}

void Node::SetNumber(const unsigned n) 
{ number=n;}

unsigned Node::GetNumber() const 
{ return number;}

void Node::SetLatency(const double l)
{ lat=l;}

double Node::GetLatency() const 
{ return lat;}

void Node::SetBW(const double b)
{ bw=b;}

double Node::GetBW() const 
{ return bw;}

Node::~Node()
{}

// Implement these functions  to post an event to the event queue in the event simulator
// so that the corresponding node can recieve the ROUTING_MESSAGE_ARRIVAL event at the proper time
void Node::SendToNeighbors(const RoutingMessage *m)
{
  
  /*deque<Node*> * neighbors = GetNeighbors();
  for(deque<Node*>::iterator i = neighbors->begin(); i != neighbors->end(); i++){
    SendToNeighbor(*i, m);
  }*/
  
  context->SendToNeighbors(this, m);
}

void Node::SendToNeighbor(const Node *n, const RoutingMessage *m)
{
  /*
  Link *l = context->FindMatchingLink(&Link(number, n->GetNumber(),0,0,0));
  
  context->PostEvent(new Event(context->GetTime()+l->GetLatency(), 
		      ROUTING_MESSAGE_ARRIVAL,
		      context->FindMatchingNode(n),
		      (void*)m));
	*/	      
    context->SendToNeighbor(this, n, m);
}

deque<Node*> *Node::GetNeighbors()
{
  return context->GetNeighbors(this);
}

void Node::SetTimeOut(const double timefromnow)
{
  context->TimeOut(this,timefromnow);
}


bool Node::Matches(const Node &rhs) const
{
  return number==rhs.number;
}


#if defined(GENERIC)
void Node::LinkHasBeenUpdated(const Link *l)
{
  cerr << *this << " got a link update: "<<*l<<endl;
  //Do Something generic:
  SendToNeighbors(new RoutingMessage);
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cerr << *this << " got a routing messagee: "<<*m<<" Ignored "<<endl;
}


void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

Node *Node::GetNextHop(const Node *destination) const
{
  return 0;
}

Table *Node::GetRoutingTable() const
{
  return new Table;
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw<<")";
  return os;
}

#endif

#if defined(LINKSTATE)

void Node::LinkHasBeenUpdated(const Link *l)
{
  if (NodeTable.isNewLink(l)) {
    NodeTable.AddLink(l);
    NodeTable.Update();
    SendToNeighbors(new RoutingMessage(l));
  }
  cerr << *this<<": Link Update: "<<*l<<endl;
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  LinkHasBeenUpdated(m->link);
  cerr << *this << " Routing Message: "<<*m;
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

Node *Node::GetNextHop(const Node *destination) 
{
  // WRITE
  unsigned nextHop = NodeTable.GetNextHop(destination->number);
  if (nextHop == UINT_MAX) {
    return 0;
  } else {
    return new Node(nextHop, 0, 0, 0);
  }
}

Table *Node::GetRoutingTable() const
{
  // WRITE
  return new Table(NodeTable);
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw<<")";
  return os;
}
#endif


#if defined(DISTANCEVECTOR)

void Node::LinkHasBeenUpdated(const Link *l)
{
  // update our table
  // send out routing mesages
  cerr << *this<<": Link Update: "<<*l<<endl;
  bool flag = nodeTable->checkNeighborLinkCost(l->GetDest(), l->GetLatency());
  cout<<"Should the link update?"<<flag<<endl;
  if(flag){
    if(number == 1){
      map<unsigned, double>::iterator i = nodeTable->getDVT(number).begin();
      for(;i != nodeTable->getDVT(number).end(); i++){
	cout<<"From "<<number<<" to "<<i->first<<" cost is "<<i->second<<endl; 
      }
    }
    
    SendToNeighbors(new RoutingMessage(number, nodeTable->getDVT(number)));
    
  }


}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cout<<"message incoming!"<<endl;
  if (nodeTable->updateTable(m->srcNode, m->vectorT)){
    cout<<"message sent!"<<endl;
    SendToNeighbors(new RoutingMessage(number, nodeTable->getDVT(number)));

  }
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}


Node *Node::GetNextHop(const Node *destination) 
{
  unsigned nextHop = nodeTable->getNextHop(destination->GetNumber());
  deque<Node*> *neighbors = GetNeighbors();
  for(deque<Node*>::iterator i = neighbors->begin(); i != neighbors->end(); i++){
    if(nextHop == (**i).GetNumber()){
      Node *nextNode = new Node(**i);
      return nextNode;
    }
  }
  return NULL;
}

Table *Node::GetRoutingTable() const
{
  Table *table = nodeTable;
  return table;
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw;
  return os;
}
#endif
