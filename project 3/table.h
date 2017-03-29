#ifndef _table
#define _table




#include <iostream>
#include <limits>
#include "limits.h"


using namespace std;

#if defined(GENERIC)
class Table {
  // Students should write this class

 public:
  ostream & Print(ostream &os) const;
};
#endif


#if defined(LINKSTATE)

#include <deque>
#include <map>
#include <vector>
#include <iterator>
#include <set>
#include "link.h"

struct data {
  data() {}
  data(unsigned n, double l) {
    nextHop = n;
    latency = l;
    }
    unsigned nextHop;
    double latency;
};

class Table {
  // Students should write this class
  public:
    unsigned num;
    vector<const Link *> links;
    map<unsigned, data> routes;
    set<unsigned> nodes;
    map<unsigned, map<unsigned, double> > graph;
    
    Table();
    
    Table(unsigned n);
    bool isNewLink(const Link* l);
    void AddLink(const Link* l);
    void Update();
    unsigned GetNextHop(unsigned d) const;
  
    ostream & Print(ostream &os) const;
};
#endif

#if defined(DISTANCEVECTOR)

#include <deque>
#include <map>
#include <iterator>
#include "link.h"

class Table {
 private:
	unsigned nodeNum;
	map<unsigned, map<unsigned, double> > routeT;
	map<unsigned, unsigned> forwardT;
	map<unsigned, double> neighborLinkCost;
	
 public:
        Table();
	Table(unsigned nodeNumber);
	
	bool updateTable(unsigned neighborNode, map<unsigned, double> vectorT);
	bool updateVector(unsigned neighborNode);
	unsigned getNextHop(unsigned dst);
	//double getDV(unsigned middle, unsigned dst);
	bool checkNeighborLinkCost(unsigned neighborNode, double linkCost);
	ostream & Print(ostream &os) const;
	map<unsigned, double> getDVT(unsigned nodeNum);

};
#endif

inline ostream & operator<<(ostream &os, const Table &t) { return t.Print(os);}

#endif
