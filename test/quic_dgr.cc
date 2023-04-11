/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <ctime>
#include <sstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/dgrv2-module.h"
#include "ns3/quic-module.h"
#include "ns3/topology-read-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/node-list.h"
#include <stdlib.h> 

#include <list>
#include <fstream>
#include <sstream>
#include <string>

using namespace ns3;

std::string expName = "dgrv2_demo";

std::string dir;
uint32_t prev = 0;
Time prevTime = Seconds(0);

NS_LOG_COMPONENT_DEFINE (expName);

// Calculate throughput
static void
TraceThroughput(Ptr<FlowMonitor> monitor)
{
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    auto itr = stats.begin();
    Time curTime = Now();
    std::ofstream thr(dir + "/throughput.dat", std::ios::out | std::ios::app);
    thr << curTime.GetSeconds() << " "
        << 8 * (itr->second.rxBytes - prev) /
               (1000 * 1000 * (curTime.GetSeconds() - prevTime.GetSeconds()))
        << std::endl;
    prevTime = curTime;
    prev = itr->second.rxBytes;
    // std::cout<<itr->second.txBytes<<std::endl;
    Simulator::Schedule(Seconds(0.01), &TraceThroughput, monitor);
}

int main (int argc, char *argv[])
{
  
  // Naming the output directory using local system time
  time_t rawtime;
  struct tm* timeinfo;
  char buff[80];
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  strftime(buff, sizeof(buff), "%Y-%m-%d-%I-%M-%S", timeinfo);
  std::string currentTime(buff);
  Time stopTime = Seconds(5.0);

  // congestion control
  std::string tcpTypeId = "TcpBbr";
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::" + tcpTypeId));
  
  
  std::string topo ("3by3");
  std::string format ("Inet");

  // Set up command line parameters used to control the experiment.
  CommandLine cmd (__FILE__);
  cmd.AddValue ("format", "Format to use for data input [Orbis|Inet|Rocketfuel].",
                format);
  cmd.AddValue ("topo", "topology", topo);
  cmd.Parse (argc, argv);
  // std::string input ("contrib/dgrv2/infocomm2023/topo/Inet_" + topo + "_topo.txt");
  std::string input ("contrib/dgrv2/infocomm2023/topo/Inet_3by3_topo_10.txt");
  // ------------------------------------------------------------
  // -- Read topology data.
  // --------------------------------------------
  
  // Pick a topology reader based in the requested format.
  TopologyReaderHelper topoHelp;
  topoHelp.SetFileName (input);
  topoHelp.SetFileType (format);
  Ptr<TopologyReader> inFile = topoHelp.GetTopologyReader ();

  NodeContainer nodes;

  if (inFile)
    {
      nodes = inFile->Read ();
    }

  if (inFile->LinksSize () == 0)
    {
      NS_LOG_ERROR ("Problems reading the topology file. Failing.");
      return -1;
    }

  // ------------------------------------------------------------
  // -- Create nodes and network stacks
  // --------------------------------------------
  NS_LOG_INFO ("creating internet stack");

  // Setup Routing algorithm
  Ipv4DGRRoutingHelper dgr;
  Ipv4ListRoutingHelper list;
  list.Add (dgr, 10);

  // Install stack
  QuicHelper stack;
  stack.SetRoutingHelper (list);
  stack.InstallQuic (nodes);
  // InternetStackHelper internet;
  // internet.SetRoutingHelper (list);
  // internet.Install (nodes);



  NS_LOG_INFO ("creating ipv4 addresses");
  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.252");

  int totlinks = inFile->LinksSize ();

  NS_LOG_INFO ("creating node containers");
  NodeContainer* nc = new NodeContainer[totlinks];
  NetDeviceContainer* ndc = new NetDeviceContainer[totlinks];
  PointToPointHelper p2p;
  TrafficControlHelper tch;
  tch.SetRootQueueDisc ("ns3::DGRVirtualQueueDisc");
  NS_LOG_INFO ("creating ipv4 interfaces");
  Ipv4InterfaceContainer* ipic = new Ipv4InterfaceContainer[totlinks];
  std::cout << "totlinks number: " << totlinks << std::endl;
  TopologyReader::ConstLinksIterator iter;
  int i = 0;
  for ( iter = inFile->LinksBegin (); iter != inFile->LinksEnd (); iter++, i++)
    {
      // std::cout << iter->GetFromNode ()->GetId () << ", to " << iter->GetToNode()->GetId () << std::endl;
      nc[i] = NodeContainer (iter->GetFromNode (), iter->GetToNode ());
      std::string delay = iter->GetAttribute ("Weight");
      std::stringstream ss;
      ss << delay;
      uint16_t metric;
      ss >> metric;
      p2p.SetChannelAttribute ("Delay", StringValue (delay + "ms"));
      p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
      ndc[i] = p2p.Install (nc[i]);
      tch.Install (ndc[i]);
      // std::cout << "delay=" <<delay << "," << "metric=" << metric << std::endl;
      ipic[i] = address.Assign (ndc[i]);
      // ipic[i].GetAddress (0).Print (std::cout);
      // std::cout << "  ";
      // ipic[i].GetAddress (1).Print (std::cout);
      // std::cout << std::endl;
      // metric in microsecond
      ipic[i].SetMetric (0, metric*1000+1);
      ipic[i].SetMetric (1, metric*1000+1);
      address.NewNetwork ();
    }



  Ipv4DGRRoutingHelper::PopulateRoutingTables ();

  // ------------------------------------------------------------
  // -- Print routing table
  // ---------------------------------------------
  Ipv4DGRRoutingHelper d;
  Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>
  (topo + expName + ".routes", std::ios::out);
  d.PrintRoutingTableAllAt (Seconds (0), routingStream);


    // -------------- Quic traffic 0-->8 ------------------
  uint16_t quicPort = 9;
  uint32_t quicSink = 8;
  uint32_t quicSender = 0;
  Ptr<Node> quicSinkNode = nodes.Get (quicSink);
  Ptr<Ipv4> ipv4QuicSink = quicSinkNode->GetObject<Ipv4> ();
  Ipv4InterfaceAddress iaddrQuicSink = ipv4QuicSink->GetAddress (1,0);
  Ipv4Address ipv4AddrQuicSink = iaddrQuicSink.GetLocal ();
  DGRSinkHelper QuicsinkHelper ("ns3::QuicSocketFactory",
                         InetSocketAddress (Ipv4Address::GetAny (), quicPort));
  ApplicationContainer QuicsinkApp = QuicsinkHelper.Install (nodes.Get (quicSink));
  QuicsinkApp.Start (Seconds (0.0));
  QuicsinkApp.Stop (Seconds (5.0));
  
  // quic sender
  Ptr<Socket> quicSocket = Socket::CreateSocket (nodes.Get (quicSender), QuicSocketFactory::GetTypeId ());
  Ptr<DGRQuicApplication> app = CreateObject<DGRQuicApplication> ();
  app->Setup (quicSocket, InetSocketAddress (ipv4AddrQuicSink, quicPort), 150, 20000, DataRate ("1Mbps"), 50, true);
  nodes.Get (quicSender)->AddApplication (app);
  app->SetStartTime (Seconds (0.0));
  app->SetStopTime (Seconds (5.0));
  // -------------- Quic end------------------


  // // -------------- Udp traffic 2-->6 ------------------
  // uint16_t udpPort = 9;
  // uint32_t udpSink = 8;
  // uint32_t udpSender = 0;
  // Ptr<Node> udpSinkNode = nodes.Get (udpSink);
  // Ptr<Ipv4> ipv4UdpSink = udpSinkNode->GetObject<Ipv4> ();
  // Ipv4InterfaceAddress iaddrUdpSink = ipv4UdpSink->GetAddress (1,0);
  // Ipv4Address ipv4AddrUdpSink = iaddrUdpSink.GetLocal ();

  // DGRSinkHelper UdpsinkHelper ("ns3::UdpSocketFactory",
  //                        InetSocketAddress (Ipv4Address::GetAny (), udpPort));
  // ApplicationContainer UdpsinkApp = UdpsinkHelper.Install (nodes.Get (udpSink));
  // UdpsinkApp.Start (Seconds (0.0));
  // UdpsinkApp.Stop (Seconds (5.0));
  
  // // udp sender
  // Ptr<Socket> udpSocket = Socket::CreateSocket (nodes.Get (udpSender), UdpSocketFactory::GetTypeId ());
  // Ptr<DGRUdpApplication> app = CreateObject<DGRUdpApplication> ();
  // app->Setup (udpSocket, InetSocketAddress (ipv4AddrUdpSink, udpPort), 150, 2000, DataRate ("1Mbps"), 50, true);
  // nodes.Get (udpSender)->AddApplication (app);
  // app->SetStartTime (Seconds (2.0));
  // app->SetStopTime (Seconds (5.0));
  
  // // -------------- TCP Back ground traffic 0-->8 ------------------
  // uint16_t tcpPort = 8080;
  // uint32_t tcpSink = 3;
  // uint32_t tcpSender = 0;
  // Ptr<Node> tcpSinkNode = nodes.Get (tcpSink);
  // Ptr<Ipv4> ipv4TcpSink = tcpSinkNode->GetObject<Ipv4> ();
  // Ipv4InterfaceAddress iaddrTcpSink = ipv4TcpSink->GetAddress (1,0);
  // Ipv4Address ipv4AddrTcpSink = iaddrTcpSink.GetLocal ();

  // DGRSinkHelper sinkHelper ("ns3::TcpSocketFactory",
  //                        InetSocketAddress (Ipv4Address::GetAny (), tcpPort));
  // ApplicationContainer sinkApp = sinkHelper.Install (nodes.Get (tcpSink));
  // sinkApp.Start (Seconds (0.0));
  // sinkApp.Stop (Seconds (5.0));
  
  // // tcp send
  // DGRTcpAppHelper sourceHelper ("ns3::TcpSocketFactory",
  //                              InetSocketAddress (ipv4AddrTcpSink, tcpPort));
  // sourceHelper.SetAttribute ("MaxBytes", UintegerValue (0));
  // // sourceHelper.SetAttribute ("Budget", UintegerValue (600000));
  // ApplicationContainer sourceApp = sourceHelper.Install (nodes.Get (tcpSender));
  // sourceApp.Start (Seconds (1.0));
  // // Hook trace source after application starts
  // sourceApp.Stop (Seconds (5.0));

  // Create a new directory to store the output of the program
  dir = "RESULTS-dgr-bbr/" + currentTime + "/";
  std::string dirToSave = "mkdir -p " + dir;
  if (system(dirToSave.c_str()) == -1)
  {
      exit(1);
  }  
  
  // Check for dropped packets using Flow Monitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  Simulator::Schedule(Seconds(1 + 0.000001), &TraceThroughput, monitor);
  
  
  // ------------------------------------------------------------
  // -- Net anim
  // ---------------------------------------------
  AnimationInterface anim (dir + "anim.xml");
  std::ifstream topoNetanim (input);
  std::istringstream buffer;
  std::string line;
  getline (topoNetanim, line);
  for (uint32_t i = 0; i < nodes.GetN (); i ++)
  {
    getline (topoNetanim, line);
    buffer.clear ();
    buffer.str (line);
    int no;
    double x, y;
    buffer >> no;
    buffer >> x;
    buffer >> y;
    anim.SetConstantPosition (nodes.Get (no), x*10, y*10);
  }

  // ------------------------------------------------------------
  // -- Run the simulation
  // --------------------------------------------
  // p2p.EnablePcapAll("myfirst");
  NS_LOG_INFO ("Run Simulation.");
  Simulator::Stop(stopTime+ TimeStep(1));
  Simulator::Run ();
  Simulator::Destroy ();

  delete[] ipic;
  delete[] ndc;
  delete[] nc;

  NS_LOG_INFO ("Done.");

  return 0;
}
