#include "../include/Poller.h"
#include "../include/Channel.h"
#include "../include/CurrentThread.h"
#include "../include/Thread.h"

Poller::Poller(EventLoop *loop) : ownerLoop_(loop) {}

bool Poller::hasChannel(Channel *channel) const {
  auto it = channels_.find(channel->fd());
  return it != channels_.end() && it->second == channel;
}