/*
 * This script simulates light TCP traffic for BLUE evaluation
 * Authors: Vivek Jain and Mohit P. Tahiliani
 * Wireless Information Networking Group
 * NITK Surathkal, Mangalore, India
*/

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv6-static-routing-helper.h"
#include "ns3/ipv6-routing-table-entry.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-header.h"
#include "ns3/traffic-control-module.h"
#include <fstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BlueTests");

std::stringstream filePlotQueue;

void
CheckQueueSize (Ptr<QueueDisc> queue)
{
  uint32_t qSize = StaticCast<BlueQueueDisc> (queue)->GetQueueSize ();

  // check queue size every 1/100 of a second
  Simulator::Schedule (Seconds (0.01), &CheckQueueSize, queue);

  std::ofstream fPlotQueue (filePlotQueue.str ().c_str (), std::ios::out|std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}

int main (int argc, char *argv[])
{

  bool printBlueStats = true;
  bool  isPcapEnabled = true;
  std::string  pathOut = ".";
  bool writeForPlot = true;
  std::string pcapFileName = "blue-udp.pcap";

  CommandLine cmd;
  cmd.Parse (argc,argv);

  LogComponentEnable ("BlueQueueDisc", LOG_LEVEL_INFO);

  std::string bottleneckBandwidth = "10Mbps";
  std::string bottleneckDelay = "50ms";

  std::string accessBandwidth = "50Mbps";
  std::string accessDelay = "5ms";

  NodeContainer udpsource;
  udpsource.Create (1);

  NodeContainer gateway;
  gateway.Create (2);

  NodeContainer sink;
  sink.Create (1);

  Config::SetDefault ("ns3::Queue::MaxPackets", UintegerValue (13));
  Config::SetDefault ("ns3::PfifoFastQueueDisc::Limit", UintegerValue (50));

  Config::SetDefault ("ns3::BlueQueueDisc::Mode", StringValue ("QUEUE_MODE_PACKETS"));
  Config::SetDefault ("ns3::BlueQueueDisc::QueueLimit", UintegerValue (200));
  Config::SetDefault ("ns3::BlueQueueDisc::FreezeTime", TimeValue (Seconds(0.1)));
  Config::SetDefault ("ns3::BlueQueueDisc::Increment", DoubleValue (0.0025));
  Config::SetDefault ("ns3::BlueQueueDisc::Decrement", DoubleValue (0.00025));
 
  NS_LOG_INFO ("Install internet stack on all nodes.");
  InternetStackHelper internet;
  internet.InstallAll ();

  TrafficControlHelper tchPfifo;
  uint16_t handle = tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");
  tchPfifo.AddInternalQueues (handle, 3, "ns3::DropTailQueue", "MaxPackets", UintegerValue (1000));

  TrafficControlHelper tchBlue;
  tchBlue.SetRootQueueDisc ("ns3::BlueQueueDisc");

// Create and configure access link and bottleneck link
  PointToPointHelper accessLink;
  accessLink.SetDeviceAttribute ("DataRate", StringValue (accessBandwidth));
  accessLink.SetChannelAttribute ("Delay", StringValue (accessDelay));
  accessLink.SetQueue ("ns3::DropTailQueue");

  NetDeviceContainer devices_sink;
  devices_sink = accessLink.Install (gateway.Get (1), sink.Get (0));
  tchPfifo.Install (devices_sink);

  PointToPointHelper bottleneckLink;
  bottleneckLink.SetDeviceAttribute ("DataRate", StringValue (bottleneckBandwidth));
  bottleneckLink.SetChannelAttribute ("Delay", StringValue (bottleneckDelay));
  bottleneckLink.SetQueue ("ns3::DropTailQueue");

  NetDeviceContainer devices_gateway;
  devices_gateway = bottleneckLink.Install (gateway.Get (0), gateway.Get (1));
  // only backbone link has Blue queue disc
  QueueDiscContainer queueDiscs = tchBlue.Install (devices_gateway);

  NS_LOG_INFO ("Assign IP Addresses");

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");

  // Configure the source and sink net devices
  // and the channels between the source/sink and the gateway
  //Ipv4InterfaceContainer sink_Interfaces;
  Ipv4InterfaceContainer interfaces_sink;
  Ipv4InterfaceContainer interfaces_gateway;
  Ipv4InterfaceContainer udpinterfaces;

  NetDeviceContainer udpdevices;

  udpdevices = accessLink.Install (udpsource.Get (0), gateway.Get (0));
  address.NewNetwork ();
  udpinterfaces = address.Assign (udpdevices);

  address.NewNetwork ();
  interfaces_gateway = address.Assign (devices_gateway);

  address.NewNetwork ();
  interfaces_sink = address.Assign (devices_sink);

  NS_LOG_INFO ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  uint16_t port = 50000;

  // Configure application
  AddressValue remoteAddress1 (InetSocketAddress (interfaces_sink.GetAddress (1), port));

  OnOffHelper clientHelper6 ("ns3::UdpSocketFactory", Address ());
  clientHelper6.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  clientHelper6.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  clientHelper6.SetAttribute ("DataRate", DataRateValue (DataRate ("25Mbps")));
  clientHelper6.SetAttribute ("PacketSize", UintegerValue (500));

  ApplicationContainer clientApps6;
  clientHelper6.SetAttribute ("Remote", remoteAddress1);
  clientApps6.Add (clientHelper6.Install (udpsource.Get (0)));
  clientApps6.Start (Seconds (1.0));
  clientApps6.Stop (Seconds (102));

  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", sinkLocalAddress);
  sinkHelper.SetAttribute ("Protocol", TypeIdValue (UdpSocketFactory::GetTypeId ()));
  ApplicationContainer sinkApp = sinkHelper.Install (sink);
  sinkApp.Start (Seconds (0));
  sinkApp.Stop (Seconds (102));

  if (writeForPlot)
    {
      filePlotQueue << pathOut << "/" << "blue-queue.plotme";
      remove (filePlotQueue.str ().c_str ());
      Ptr<QueueDisc> queue = queueDiscs.Get (0);
      Simulator::ScheduleNow (&CheckQueueSize, queue);
    }

  if (isPcapEnabled)
    {
      bottleneckLink.EnablePcap (pcapFileName,gateway,true);
    }

  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> allMon;
  allMon = flowmon.InstallAll ();

  flowmon.SerializeToXmlFile ("blue-udp.xml", true, true);

  Simulator::Stop (Seconds (104));
  Simulator::Run ();

  if (printBlueStats)
    {
      BlueQueueDisc::Stats st1 = StaticCast<BlueQueueDisc> (queueDiscs.Get (0))->GetStats ();
      std::cout << "*** Blue stats from Node 2 queue ***" << std::endl;
      std::cout << "\t " << st1.unforcedDrop << " drops due to probability " << std::endl;
      std::cout << "\t " << st1.forcedDrop << " drops due queue full" << std::endl;


      Ptr<PointToPointNetDevice> nd2 = StaticCast<PointToPointNetDevice> (devices_gateway.Get (1));
      BlueQueueDisc::Stats st2 = StaticCast<BlueQueueDisc> (queueDiscs.Get (1))->GetStats ();
      std::cout << "*** Blue stats from Node 2 queue ***" << std::endl;
      std::cout << "\t " << st2.unforcedDrop << " drops due to probability " << std::endl;
      std::cout << "\t " << st2.forcedDrop << " drops due queue full" << std::endl;

    }

  Simulator::Destroy ();
  return 0;
}
