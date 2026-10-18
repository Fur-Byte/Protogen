#include "ApplicationStatus.h"
#include <cassert>
#include <iostream>
using HelmetLink::Packet;
static Packet status(uint32_t sequence,uint8_t expression) {
 Packet p;p.kind=HelmetLink::Status;p.length=HelmetLink::StatusBytes;p.epoch=9;p.sequence=sequence;p.ticket=sequence;
 HelmetLink::put32(p.payload,3);p.payload[4]=expression;p.payload[5]=HelmetLink::Visible|HelmetLink::DisplayReady;return p;
}
static Packet command() {
 Packet p;p.kind=HelmetLink::Command;p.length=8;p.epoch=9;p.sequence=7;p.payload[4]=HelmetLink::Expression;p.payload[5]=1;HelmetLink::put32(p.payload,3);return p;
}
int main() {
 Wireless::UartStatusFence fence;auto cmd=command();fence.queued(cmd);
 assert(!fence.status(status(11,0))); // Pre-ACK status cannot republish the old expression.
 Packet ack;ack.kind=HelmetLink::Ack;ack.length=5;ack.epoch=9;ack.sequence=6;
 assert(!fence.ack(ack));assert(!fence.status(status(12,0)));
 ack.sequence=7;assert(fence.ack(ack));assert(fence.status(status(13,1))&&!fence.waiting());
 Wireless::HelmetStatus paw;bool changed=false;assert(paw.receive(status(10,0),100,changed));
 paw.expectAfterAck(cmd,HelmetLink::Accepted,110);
 assert(!paw.receive(status(11,0),115,changed)&&!paw.available);
 assert(paw.receive(status(13,1),120,changed)&&paw.available);
 assert((paw.packet.payload[4]+1)%6==2); // A second Next uses the post-command expression.
 assert(!paw.receive(status(12,0),125,changed)); // Delayed authenticated status cannot regress it.
 paw.expectAfterAck(cmd,HelmetLink::Accepted,130);
 auto reboot=status(1,4);reboot.epoch=10;
 assert(paw.receive(reboot,131,changed)&&changed);
 // Local controls can legitimately override the commanded expression. Bounded
 // settling must not become a permanent lockout when that happens.
 cmd.epoch=10;paw.expectAfterAck(cmd,HelmetLink::Accepted,UINT32_MAX-10);
 auto override=status(2,4);override.epoch=10;
 assert(!paw.receive(override,10,changed));
 assert(paw.receive(override,200,changed));
 fence.queued(cmd,100);assert(fence.retry(cmd));
 auto changedCommand=cmd;changedCommand.payload[5]=2;assert(!fence.retry(changedCommand));
 fence.queued(cmd,200);assert(!fence.poll(399));assert(fence.poll(400)&&!fence.waiting());
 fence.queued(cmd);reboot.epoch=11;assert(fence.status(reboot)&&!fence.waiting());
 std::cout<<"PASS: post-ACK fresh status, queued Next, reordered status, epoch change and bounded local override\n";
}
