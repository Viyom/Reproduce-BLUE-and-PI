/*
 * This script simulates mix TCP and UDP traffic for PI evaluation
 * Authors: Shravya K.S, Smriti Murali and Mohit P. Tahiliani
 * Wireless Information Networking Group
 * NITK Surathkal, Mangalore, India
*/

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include <fstream>
#include "ns3/ipv6-static-routing-helper.h"
#include "ns3/ipv6-routing-table-entry.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/tcp-header.h"
#include "ns3/traffic-control-module.h"
#include  <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PiTests");

std::stringstream filePlotQueue;
std::stringstream filePlotQueueAvg;
std::stringstream filePlotThroughput;
std::stringstream filePlotDrops;
uint32_t i = 0;
uint32_t checkTimes;
double avgQueueSize;


void
CheckQueueSize (Ptr<QueueDisc> queue)
{
  uint32_t qSize = StaticCast<PiQueueDisc> (queue)->GetQueueSize ();

  avgQueueSize += qSize;
  checkTimes++;

  // check queue size every 1/100 of a second
  Simulator::Schedule (Seconds (0.1), &CheckQueueSize, queue);

  std::ofstream fPlotQueue (filePlotQueue.str ().c_str (), std::ios::out|std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();

  std::ofstream fPlotQueueAvg (filePlotQueueAvg.str ().c_str (), std::ios::out|std::ios::app);
  fPlotQueueAvg << Simulator::Now ().GetSeconds () << " " << avgQueueSize / checkTimes << std::endl;
  fPlotQueueAvg.close ();
}
/*
void
CheckThroughput (Ptr<QueueDisc> queue)
{
  uint32_t qThroughput = StaticCast<PiQueueDisc> (queue)->GetThroughput ();
  // check throughput every 1/10 of a second
  Simulator::Schedule (Seconds (0.1), &CheckThroughput, queue);

  std::ofstream fPlotThroughput (filePlotThroughput.str ().c_str (), std::ios::out|std::ios::app);
  fPlotThroughput << Simulator::Now ().GetSeconds () << " " << qThroughput << std::endl;
  fPlotThroughput.close ();
}
*/
void
CheckDrops (Ptr<QueueDisc> queue)
{
  PiQueueDisc::Stats st = StaticCast<PiQueueDisc> (queue)->GetStats ();
  uint32_t qDrops = st.unforcedDrop + st.forcedDrop;
  // check dropCount every 1/100 of a second
  Simulator::Schedule (Seconds (0.01), &CheckDrops, queue);

  std::ofstream fPlotDrops (filePlotDrops.str ().c_str (), std::ios::out|std::ios::app);
  fPlotDrops << Simulator::Now ().GetSeconds () << " " << qDrops << std::endl;
  fPlotDrops.close ();
}

int main (int argc, char *argv[])
{
  bool printPiStats = true;
  bool  isPcapEnabled = true;
  float startTime = 0.0;
  float simDuration = 101;      // in seconds
  std::string  pathOut = ".";
  bool writeForPlot = true;
  std::string pcapFileName = "third-mix.pcap";

  float stopTime = startTime + simDuration;

  CommandLine cmd;
  cmd.Parse (argc,argv);

  LogComponentEnable ("PiQueueDisc", LOG_LEVEL_INFO);

  std::string bottleneckBandwidth = "10Mbps";
  std::string bottleneckDelay = "50ms";

  std::string accessBandwidth = "10Mbps";
  std::string accessDelay = "5ms";


  NodeContainer source;
  source.Create (5);

  NodeContainer udpsource;
  udpsource.Create (2);

  NodeContainer gateway;
  gateway.Create (2);

  NodeContainer sink;
  sink.Create (1);

  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (1));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1000));
  Config::SetDefault ("ns3::TcpSocketBase::WindowScaling", BooleanValue (true));

  Config::SetDefault ("ns3::Queue::MaxPackets", UintegerValue (13));
  Config::SetDefault ("ns3::TcpSocket::DelAckTimeout", TimeValue(Seconds (0)));
  Config::SetDefault ("ns3::PfifoFastQueueDisc::Limit", UintegerValue (50));

//=========
  Config::SetDefault ("ns3::TcpSocketBase::LimitedTransmit", BooleanValue (false));

  Config::SetDefault ("ns3::PiQueueDisc::MeanPktSize", UintegerValue (1000));
  Config::SetDefault ("ns3::PiQueueDisc::Mode", StringValue ("QUEUE_MODE_PACKETS"));
  Config::SetDefault ("ns3::PiQueueDisc::QueueRef", DoubleValue (50));
  Config::SetDefault ("ns3::PiQueueDisc::QueueLimit", DoubleValue (200));

  NS_LOG_INFO ("Install internet stack on all nodes.");
  InternetStackHelper internet;
  internet.InstallAll ();

  TrafficControlHelper tchPfifo;
  uint16_t handle = tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");
  tchPfifo.AddInternalQueues (handle, 3, "ns3::DropTailQueue", "MaxPackets", UintegerValue (1000));

  TrafficControlHelper tchPi;
  tchPi.SetRootQueueDisc ("ns3::PiQueueDisc");

  // Create and configure access link and bottleneck link
  PointToPointHelper accessLink;
  accessLink.SetQueue ("ns3::DropTailQueue");
  accessLink.SetDeviceAttribute ("DataRate", StringValue (accessBandwidth));
  accessLink.SetChannelAttribute ("Delay", StringValue (accessDelay));

  NetDeviceContainer devices[5];
  for (i = 0; i < 5; i++)
    {
      devices[i] = accessLink.Install (source.Get (i), gateway.Get (0));
      tchPfifo.Install (devices[i]);
    }

  NetDeviceContainer devices_sink;
  devices_sink = accessLink.Install (gateway.Get (1), sink.Get (0));
  tchPfifo.Install (devices_sink);

  PointToPointHelper bottleneckLink;
  bottleneckLink.SetQueue ("ns3::DropTailQueue");
  bottleneckLink.SetDeviceAttribute ("DataRate", StringValue (bottleneckBandwidth));
  bottleneckLink.SetChannelAttribute ("Delay", StringValue (bottleneckDelay));

  NetDeviceContainer devices_gateway;
  devices_gateway = bottleneckLink.Install (gateway.Get (0), gateway.Get (1));
  // only backbone link has Pi queue disc
  QueueDiscContainer queueDiscs = tchPi.Install (devices_gateway);

  NS_LOG_INFO ("Assign IP Addresses");

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");

  // Configure the source and sink net devices
  // and the channels between the source/sink and the gateway
  //Ipv4InterfaceContainer sink_Interfaces;
  Ipv4InterfaceContainer interfaces[5];
  Ipv4InterfaceContainer interfaces_sink;
  Ipv4InterfaceContainer interfaces_gateway;
  Ipv4InterfaceContainer udpinterfaces[2];

  NetDeviceContainer udpdevices[2];




  for (i = 0; i < 5; i++)
    {
      //devices[i] = accessLink.Install (source.Get (i), gateway.Get (0));
      address.NewNetwork ();
      interfaces[i] = address.Assign (devices[i]);
    }

  for (i = 0; i < 2; i++)
    {
      udpdevices[i] = accessLink.Install (udpsource.Get (i), gateway.Get (0));
      address.NewNetwork ();
      udpinterfaces[i] = address.Assign (udpdevices[i]);
    }
  //devices_gateway = bottleneckLink.Install (gateway.Get (0), gateway.Get (1));
  address.NewNetwork ();
  interfaces_gateway = address.Assign (devices_gateway);

  //devices_sink = accessLink.Install (gateway.Get (1), sink.Get (0));
  address.NewNetwork ();
  interfaces_sink = address.Assign (devices_sink);

  //sink_Interfaces.Add (interfaces_sink.Get (1));

  NS_LOG_INFO ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


  uint16_t port = 50000;
  uint16_t port1 = 50001;
  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
  Address sinkLocalAddress1 (InetSocketAddress (Ipv4Address::GetAny (), port1));
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
  PacketSinkHelper sinkHelper1 ("ns3::UdpSocketFactory", sinkLocalAddress1);


  // Configure application
  //AddressValue remoteAddress (InetSocketAddress (sink_Interfaces.GetAddress (0, 0), port));
  //AddressValue remoteAddress1 (InetSocketAddress (sink_Interfaces.GetAddress (0, 0), port1));
  AddressValue remoteAddress (InetSocketAddress (interfaces_sink.GetAddress (1), port));
  AddressValue remoteAddress1 (InetSocketAddress (interfaces_sink.GetAddress (1), port1));
  for (uint16_t i = 0; i < source.GetN (); i++)
    {
      Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1000));
      BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
      ftp.SetAttribute ("Remote", remoteAddress);
      ftp.SetAttribute ("SendSize", UintegerValue (1000));


      ApplicationContainer sourceApp = ftp.Install (source.Get (i));
      sourceApp.Start (Seconds (0));
      sourceApp.Stop (Seconds (stopTime - 1));

      sinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
      ApplicationContainer sinkApp = sinkHelper.Install (sink);
      sinkApp.Start (Seconds (0));
      sinkApp.Stop (Seconds (stopTime));

    }
  OnOffHelper clientHelper6 ("ns3::UdpSocketFactory", Address ());
  clientHelper6.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  clientHelper6.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  clientHelper6.SetAttribute ("DataRate", DataRateValue (DataRate ("10Mb/s")));
  clientHelper6.SetAttribute ("PacketSize", UintegerValue (1000));

  ApplicationContainer clientApps6;

  clientHelper6.SetAttribute ("Remote", remoteAddress1);
  clientApps6.Add (clientHelper6.Install (udpsource.Get (0)));
  clientApps6.Start (Seconds (0));
  clientApps6.Stop (Seconds (stopTime - 1));

  OnOffHelper clientHelper7 ("ns3::UdpSocketFactory", Address ());
  clientHelper7.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  clientHelper7.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  clientHelper7.SetAttribute ("DataRate", DataRateValue (DataRate ("10Mb/s")));
  clientHelper7.SetAttribute ("PacketSize", UintegerValue (1000));

  ApplicationContainer clientApps7;
  clientHelper7.SetAttribute ("Remote", remoteAddress1);
  clientApps7.Add (clientHelper7.Install (udpsource.Get (1)));
  clientApps7.Start (Seconds (0));
  clientApps7.Stop (Seconds (stopTime - 1));

  sinkHelper1.SetAttribute ("Protocol", TypeIdValue (UdpSocketFactory::GetTypeId ()));
  ApplicationContainer sinkApp1 = sinkHelper1.Install (sink);
  sinkApp1.Start (Seconds (0));
  sinkApp1.Stop (Seconds (stopTime));



  if (writeForPlot)
    {
     
      filePlotQueue << pathOut << "/" << "pi-queue3.plotme";
      filePlotQueueAvg << pathOut << "/" << "pi-queue_avg3.plotme";
//      filePlotThroughput << pathOut << "/" << "pi-throughput3.plotme";
      filePlotDrops << pathOut << "/" << "pi-drops3.plotme";

      remove (filePlotQueue.str ().c_str ());
      remove (filePlotQueueAvg.str ().c_str ());
//      remove (filePlotThroughput.str ().c_str ());
      remove (filePlotDrops.str ().c_str ());
      Ptr<QueueDisc> queue = queueDiscs.Get (0);
      Simulator::ScheduleNow (&CheckQueueSize, queue);
//      Simulator::ScheduleNow (&CheckThroughput, queue);
      Simulator::ScheduleNow (&CheckDrops, queue);

    }


  if (isPcapEnabled)
    {
      bottleneckLink.EnablePcap (pcapFileName,gateway,false);
    }


  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> allMon;
  allMon = flowmon.InstallAll ();


  flowmon.SerializeToXmlFile ("third-mix.xml", true, true);

  Simulator::Stop (Seconds (stopTime));
  Simulator::Run ();

  if (printPiStats)
    {
      PiQueueDisc::Stats st1 = StaticCast<PiQueueDisc> (queueDiscs.Get (0))->GetStats ();
      std::cout << "*** pi stats from Node 2 queue ***" << std::endl;
      std::cout << "\t " << st1.unforcedDrop << " drops due to probability " << std::endl;
      std::cout << "\t " << st1.forcedDrop << " drops due queue full" << std::endl;


      PiQueueDisc::Stats st2 = StaticCast<PiQueueDisc> (queueDiscs.Get (1))->GetStats ();
      std::cout << "*** pi stats from Node 2 queue ***" << std::endl;
      std::cout << "\t " << st2.unforcedDrop << " drops due to probability " << std::endl;
      std::cout << "\t " << st2.forcedDrop << " drops due queue full" << std::endl;

    }

  Simulator::Destroy ();
  return 0;
}
