#include "table.h"

#if defined(GENERIC)
ostream & Table::Print(ostream &os) const
{
  // WRITE THIS
  os << "Table()";
  return os;
}
#endif

#if defined(LINKSTATE)

Table::Table()
{
}

Table::Table(unsigned n)
{
  num = n;
  links = vector<const Link*>();
  routes = map<unsigned, data>();
}

bool Table::isNewLink(const Link* l)
{
  for (vector<const Link*>::const_iterator it = links.begin(); it != links.end(); it++) {
    cout << "Node: " << num << endl;
    cout << (*it)->GetSrc() << "=>" << (*it)->GetDest() << endl;
    if ((*it)->GetSrc() == l->GetSrc() && (*it)->GetDest() == l->GetDest() && (*it)->GetLatency() == l->GetLatency()) {
      return false;
    }
  }
  return true;
}

void Table::AddLink(const Link* l) 
{
  links.push_back(l);
  unsigned src = l->GetSrc();
  unsigned dest = l->GetDest();
  graph[src][dest] = l->GetLatency();
  nodes.insert(src);
  nodes.insert(dest);
  cout << "inserted!" << endl;
}

void Table::Update()
{
  map<unsigned, double> cost;
  map<unsigned, unsigned> prev;
  set<unsigned> N(nodes);
  for (set<unsigned>::const_iterator it = nodes.begin(); it != nodes.end(); it++) {
    cost[(*it)] = UINT_MAX;
    prev[(*it)] = UINT_MAX;
  }
  cost[num] = 0;
  while (N.size() > 0) {
    set<unsigned>::iterator minit = N.begin();
    double mincost = cost[(*minit)];
    for (set<unsigned>::const_iterator it = N.begin(); it != N.end(); it++) {
      if (mincost > cost[(*it)]) {
	mincost = cost[(*it)];
	minit = it;
      }
    }
    for (map<unsigned, double>::const_iterator itr = graph[(*minit)].begin(); itr != graph[(*minit)].end(); itr++) {
      if (N.count(itr->first) == 1) {
	double newcost = cost[(*minit)] + itr->second;
	if (newcost < cost[itr->first]) {
	  cost[itr->first] = newcost;
	  prev[itr->first] = *minit;
	}
      }
    }
    N.erase(minit);
  }
  routes.clear();
  for (set<unsigned>::const_iterator it = nodes.begin(); it != nodes.end(); it++) {
    if ((*it) == num) {
      continue;
    }
    unsigned n = *it;
    while (prev[n] != num) {
      n = prev[n];
    }
    routes[(*it)] = data(n, cost[(*it)]);
  }
}

unsigned Table::GetNextHop(unsigned d) const
{
  map<unsigned, data>::const_iterator it = routes.find(d);
  if (it != routes.end()) {
    return it->second.nextHop;
  } else {
    return UINT_MAX;
  }
}

#endif

#if defined(DISTANCEVECTOR)
Table::Table()
{
}

ostream & Table::Print(ostream &os) const
{
  os << "Distance Vector Route Table: \n";
  map<unsigned, map<unsigned, double> >::const_iterator i = routeT.begin();
  for (; i != routeT.end(); ++i){
    for(map<unsigned, double>::const_iterator j = i->second.begin(); j != i->second.end(); ++j){
      os << "Node: " << i->first <<"to Node: "<< j->first<< " Latency is: " << j->second << "\n";
    }
  }
  os << "Routing Table: \n";
  map<unsigned, unsigned>::const_iterator itr = forwardT.begin();
  for (; itr != forwardT.end(); ++itr) 
  {
    os << "Destination: " << itr->first << " Next Hop: " << itr->second << "\n";
  }
  return os;
}

Table::Table(unsigned nodeNumber){
  cout<<"Building table!"<<endl;
  map<unsigned, double> newMap;
  newMap.insert(make_pair(nodeNumber, 0.0));
  routeT.insert(make_pair(nodeNumber, newMap));
  nodeNum = nodeNumber;
}

bool Table::updateTable(unsigned neighborNode, map<unsigned, double> vectorT){
  cout<<"Updating tables!"<<endl;
  if(routeT.find(neighborNode) != routeT.end()){
    routeT.erase(neighborNode);
  }
  routeT.insert(make_pair(neighborNode, vectorT));
  //update or add thisRow in routeT
  map<unsigned, map<unsigned,double> >::iterator thisRow = routeT.find(nodeNum);
  for(map<unsigned, double>::iterator i = vectorT.begin(); i != vectorT.end(); i++){
    unsigned newNodeNum = i->first;
    map<unsigned, double>::iterator newNode = thisRow->second.find(newNodeNum);
    if(newNode == thisRow->second.end()){
      thisRow->second.insert(make_pair(newNodeNum, numeric_limits<double>::infinity()));
    }
  }
  
  return updateVector(nodeNum);

}

bool Table::updateVector(unsigned neighborNode){
  cout<<"Updating Vector Row!"<<endl;

  bool flag = false;
  map<unsigned, map<unsigned, double> >::iterator thisRow = routeT.find(nodeNum);
  for(map<unsigned, double> ::iterator i = thisRow->second.begin(); i != thisRow->second.end();i++){
    unsigned dst = i->first;
    if(dst == nodeNum){
      continue;
    }
    double pastCost = i->second;
    double minCost = numeric_limits<double>::infinity();
    unsigned forward = UINT_MAX;
    for(map<unsigned, map<unsigned, double> > ::iterator j = routeT.begin(); j != routeT.end(); j++){
      unsigned middle = j->first;
      if (middle == nodeNum){
	continue;
      }
      double toNeighbor = (neighborLinkCost.find(middle) != neighborLinkCost.end())?neighborLinkCost.find(middle)->second:numeric_limits<double>::infinity();
      double toDst = (j->second.find(dst) != j->second.end())?j->second.find(dst)->second:numeric_limits<double>::infinity();
      
      //cout<<"Updating Node:"<<nodeNum<<" dst is "<<dst<<" middle is "<<middle<<" pastCost is "<<pastCost<<endl;
      /*if(j->second.find(dst) != j->second.end()){
	D = j->second.find(dst)->second;
      }else{
	D = numeric_limits<double>::infinity();
      }*/
      //double currentCost = thisRow->second.find(middle)->second + D;
      double currentCost = toNeighbor + toDst;
      if (minCost >= currentCost){
	/*if(thisRow->second.find(dst) == thisRow->second.end()){
	  thisRow->second.insert(make_pair(dst, currentCost));
	}else{
	  thisRow->second.find(dst)->second = currentCost;
	}*/
	minCost = currentCost;
	forward = middle;
      }
    }
     if(minCost != pastCost){
      flag = true;
      if(thisRow->second.find(dst) == thisRow->second.end()){
	cout<<"new else inserted!"<<endl;
	  routeT.find(nodeNum)->second.insert(make_pair(dst, minCost));
      }else{
	  cout<<"else inserted!"<<endl;
	  routeT.find(nodeNum)->second.find(dst)->second = minCost;
	  cout<<"inserted dst : "<<routeT.find(nodeNum)->second.find(dst)->first<<" cost: "<<routeT.find(nodeNum)->second.find(dst)->second<<endl;
      }
      if(forwardT.find(dst) != forwardT.end()){
	  forwardT.erase(dst);
      }
	cout<<"("<<nodeNum<<", "<< dst<<"ForwardT is inserted:"<<forward<<endl;
	cout<<"Cost is "<<minCost<<endl;
	forwardT.insert(make_pair(dst, forward));
    }
	//i->second = currentCost;
    
  }
  return flag;
}

bool Table::checkNeighborLinkCost(unsigned neighborNode, double linkCost){
  cout<<"Checking Neighbor Link Cost!"<<endl;
  map<unsigned, double>::iterator i = neighborLinkCost.find(neighborNode);
 /* if(forwardT.find(neighborNode) != forwardT.end()){
    forwardT.erase(neighborNode);
  }*/
  
  if(i == neighborLinkCost.end()){
    cout<<"Adding new linkcost!"<<endl;
    neighborLinkCost.insert(make_pair(neighborNode, linkCost));
    //forwardT.insert(make_pair(neighborNode, neighborNode));
    map<unsigned, double> newNode;
    newNode.insert(make_pair(neighborNode, 0.0));
    //newNode.insert(make_pair(nodeNum, linkCost));//not sure if the linkCost is duplex
    //routeT.find(nodeNum)->second.insert(make_pair(neighborNode, linkCost));
    routeT.insert(make_pair(neighborNode, newNode));
    map<unsigned, map<unsigned, double> >::iterator thisRow = routeT.find(nodeNum);
    if(thisRow->second.find(neighborNode) == thisRow->second.end()){
      thisRow->second.insert(make_pair(neighborNode, numeric_limits<double>::infinity()));
    }
    
    return updateVector(nodeNum);
    
  }else{
    cout<<"Update linkcost!"<<endl;
    if(i->second != linkCost){
      i->second = linkCost;
      if(routeT.find(nodeNum)->second.find(neighborNode) != routeT.find(nodeNum)->second.end()){
      //routeT.find(nodeNum)->second.find(neighborNode)->second = linkCost;
	routeT.find(nodeNum)->second.erase(neighborNode);
      }
      routeT.find(nodeNum)->second.insert(make_pair(neighborNode, linkCost));
      return updateVector(nodeNum);
      
    }else{
      return false;
    }
  }
  
  
}

unsigned Table::getNextHop(unsigned dst){
  cout<<"Destination is "<<dst<<endl;
  cout<<"Node of forwardT is :"<<nodeNum<<endl;
  map<unsigned, unsigned>::iterator i = forwardT.begin();
  for(; i != forwardT.end(); i++){
    cout<<"Dst is :"<<i->first<<"next hop is :"<<i->second<<endl;
  }
  return forwardT.find(dst)->second;
}

map<unsigned, double> Table::getDVT(unsigned nodeNum){
  return routeT.find(nodeNum)->second;
}
  

#endif
