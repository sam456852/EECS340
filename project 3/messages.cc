#include "messages.h"


#if defined(GENERIC)
ostream &RoutingMessage::Print(ostream &os) const
{
  os << "RoutingMessage()";
  return os;
}
#endif


#if defined(LINKSTATE)

ostream &RoutingMessage::Print(ostream &os) const
{
  os << "RoutingMessage is: \n" << *link << "\n";
  return os;
}

RoutingMessage::RoutingMessage()
{}

RoutingMessage::RoutingMessage(const Link* l)
{
  link = l;
}

RoutingMessage::RoutingMessage(const RoutingMessage &rhs)
{
  link = rhs.link;
}

RoutingMessage &RoutingMessage::operator=(const RoutingMessage &rhs) 
{
  return *(new(this) RoutingMessage(rhs));
}
#endif


#if defined(DISTANCEVECTOR)

ostream &RoutingMessage::Print(ostream &os) const
{
  return os;
}

RoutingMessage::RoutingMessage()
{}


RoutingMessage::RoutingMessage(const RoutingMessage &rhs)
{}

RoutingMessage::RoutingMessage(const unsigned nodeNum, map<unsigned, double> table){
  srcNode = nodeNum;
  vectorT = table;
}

#endif

