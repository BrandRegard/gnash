// 
//   Copyright (C) 2005, 2006 Free Software Foundation, Inc.
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
// 
// Linking Gnash statically or dynamically with other modules is making
// a combined work based on Gnash. Thus, the terms and conditions of
// the GNU General Public License cover the whole combination.
// 
// In addition, as a special exception, the copyright holders of Gnash give
// you permission to combine Gnash with free software programs or
// libraries that are released under the GNU LGPL and/or with Mozilla, 
// so long as the linking with Mozilla, or any variant of Mozilla, is
// through its standard plug-in interface. You may copy and distribute
// such a system following the terms of the GNU GPL for Gnash and the
// licenses of the other code concerned, provided that you include the
// source code of that other code when and as the GNU GPL requires
// distribution of source code. 
// 
// Note that people who make modified versions of Gnash are not obligated
// to grant this special exception for their modified versions; it is
// their choice whether to do so.  The GNU General Public License gives
// permission to release a modified version without this exception; this
// exception also makes it possible to release a modified version which
// carries forward this exception.
//
//

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <SDL.h>

#include "utility.h"
#include "log.h"
#include "xml.h"
#include "network.h"

#include <sys/types.h>
#ifdef HAVE_WINSOCK_H
# include <winsock2.h>
# include <windows.h>
# include <fcntl.h>
# include <sys/stat.h>
# include <io.h>
#else
# include <sys/time.h>
# include <sys/fcntl.h>
# include <unistd.h>
# include <sys/select.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <sys/socket.h>
# include <netdb.h>
# include <errno.h>
# include <sys/param.h>
# include <sys/select.h>
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

namespace gnash {

static const int SOCKET_DATA    = 1;  
static const int INBUF          = 10000;
static const char *DEFAULTPROTO = "tcp";
static const short DEFAULTPORT  = RTMP;
static const int DEFAULTTIMEOUT = 5;
static const int BLOCKING_TIMEOUT= -1;

#ifndef INADDR_NONE
#define INADDR_NONE  0xffffffff
#endif
  
Network::Network() : _ipaddr(INADDR_ANY), _sockfd(0), _listenfd(0), _port(0), _connected(false), _debug(false)
{
    //log_msg("%s: \n", __PRETTY_FUNCTION__);
}

Network::~Network() 
{
    //log_msg("%s: \n", __PRETTY_FUNCTION__);
}

// Description: Create a tcp/ip network server. This creates a server
//              that listens for incoming socket connections. This
//              support IP aliasing on the host, and will sequntially
//              look for IP address to bind this port to.
bool
Network::createServer(void)
{
    log_msg("%s: \n", __PRETTY_FUNCTION__);
    return createServer(DEFAULTPORT);
}

bool
Network::createServer(short port)
{
    log_msg("%s: \n", __PRETTY_FUNCTION__);
    struct protoent *ppe;
    struct sockaddr_in sock_in;
    int             on, type;
    int             retries = 0;
    const struct hostent  *host;
    struct in_addr  *thisaddr, newaddr;
    in_addr_t       nodeaddr, netaddr;
  
    host = gethostbyname("localhost");
    thisaddr = reinterpret_cast<struct in_addr *>(host->h_addr_list[0]);
    _ipaddr = thisaddr->s_addr;
    memset(&sock_in, 0, sizeof(sock_in));

#if 0
    // Accept incoming connections only on our IP number
    sock_in.sin_addr.s_addr = thisaddr->s_addr;
#else
    // Accept incoming connections on any IP number
    sock_in.sin_addr.s_addr = INADDR_ANY;
#endif
    
    _ipaddr = sock_in.sin_addr.s_addr;
    sock_in.sin_family = AF_INET;
    sock_in.sin_port = htons(port);
  
    if ((ppe = getprotobyname(DEFAULTPROTO)) == 0) {
        // error, wasn't able to get a protocol entry
        log_msg("WARNING: unable to get protocol entry for %s\n",
                DEFAULTPROTO);
        return false;
    }
    
    // set protocol type
    if (DEFAULTPROTO == "udp") {
        type = SOCK_DGRAM;
    } else {
        type = SOCK_STREAM;
    }
    
    // Get a file descriptor for this socket connection
    _listenfd = socket(PF_INET, type, ppe->p_proto);
    
    // error, wasn't able to create a socket
    if (_listenfd < 0) {
        log_msg("unable to create socket: %s\n", strerror(errno));
        return true;
    }
    
    on = 1;
    if (setsockopt(_listenfd, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&on, sizeof(on)) < 0) {
        log_msg("setsockopt SO_REUSEADDR failed!\n");
        return false;
    }
    
    retries = 0;
    
    nodeaddr = inet_lnaof(*thisaddr);
    while (retries < 5) {
        if (bind(_listenfd, reinterpret_cast<struct sockaddr *>(&sock_in),
                 sizeof(sock_in)) == -1) {
            log_msg("WARNING: unable to bind to %s port! %s\n",
                    inet_ntoa(sock_in.sin_addr), strerror(errno));
            retries++;
        }

#if 0
        char                ascip[32];
        inet_ntop(AF_INET, &_ipaddr, ascip, INET_ADDRSTRLEN);
        log_msg("Host Name is %s, IP is %s", host->h_name, ascip);
#endif
    
        log_msg("Server bound to service on port: %hd, %s using fd #%d\n",
                ntohs(sock_in.sin_port), inet_ntoa(sock_in.sin_addr),
                _listenfd);
        
        if (type == SOCK_STREAM && listen(_listenfd, 5) < 0) {
            log_msg("ERROR: unable to listen on port: %d: %s ",
                port, strerror(errno));
            return false;
        }

        _port = port;
    
#if 0
        log_msg("Listening for net traffic on fd #%d\n", _sockfd);
#endif
    
        return true;
    }
    return false;
}

// Description: Accept a new network connection for the port we have
//              created a server for.
// The default is to block.
bool
Network::newConnection(void)
{
    log_msg("%s: \n", __PRETTY_FUNCTION__);

    return newConnection(true);
}

bool
Network::newConnection(bool block)
{
    log_msg("%s: \n", __PRETTY_FUNCTION__);
    struct sockaddr	fsin;
    socklen_t		alen;
    int			ret;
    struct timeval        tval;
    fd_set                fdset;
    int                   retries = 3;
  
    alen = sizeof(struct sockaddr_in);
  
#ifdef NET_DEBUG
    log_msg("Trying to accept net traffic on fd #%d\n", _sockfd);
#endif
  
    if (_listenfd <= 2) {
        return false;
    }
  
    while (retries--) {
        // We use select to wait for the read file descriptor to be
        // active, which means there is a client waiting to connect.
        FD_ZERO(&fdset);
        // also return on any input from stdin
//         if (_console) {
//             FD_SET(fileno(stdin), &fdset);
//         }
        FD_SET(_listenfd, &fdset);
    
        // Reset the timeout value, since select modifies it on return. To
        // block, set the timeout to zero.
        tval.tv_sec = 1;
        tval.tv_usec = 0;
    
        if (block) {
            ret = select(_listenfd+1, &fdset, NULL, NULL, NULL);
        } else {
            ret = select(_listenfd+1, &fdset, NULL, NULL, &tval);
        }
    
        if (FD_ISSET(0, &fdset)) {
            log_msg("There is data at the console for stdin!");
            return true;
        }

        // If interupted by a system call, try again
        if (ret == -1 && errno == EINTR) {
            log_msg("The accept() socket for fd #%d was interupted by a system call!\n", _listenfd);
        }
    
        if (ret == -1) {
            log_msg("ERROR: The accept() socket for fd #%d never was available for writing!",
                    _listenfd);
            return false;
        }
    
        if (ret == 0) {
            if (_debug) {
                log_msg("ERROR: The accept() socket for fd #%d timed out waiting to write!\n",
                        _listenfd);
            }
        }
    }

#ifndef HAVE_WINSOCK_H
    fcntl(_listenfd, F_SETFL, O_NONBLOCK); // Don't let accept() block
#endif
    _sockfd = accept(_listenfd, &fsin, &alen);
  
    if (_sockfd < 0) {
        log_msg("unable to accept : %s\n", strerror(errno));
        return false;
    }
  
    log_msg("Accepting tcp/ip connection on fd #%d\n", _sockfd);

    return true;
}

// Create a client connection to a tcp/ip based service
bool
Network::createClient(void)
{
    return createClient("localhost", RTMP);
}
bool
Network::createClient(short port)
{
    return false;
}

bool
Network::createClient(const char *hostname)
{
    return createClient(hostname, RTMP);        
}
    
bool
Network::createClient(const char *hostname, short port)
{
    struct sockaddr_in  sock_in;
    fd_set              fdset;
    struct timeval      tval;
    int                 ret;
    int                 retries;
    char                thishostname[MAXHOSTNAMELEN];
    struct protoent     *proto;

    if (port < 1024) {
        log_error("Can't connect to priviledged port #%hd!\n", port);
        _connected = false;
        return false;
    }

    log_msg("%s: to host %s at port %d\n", __FUNCTION__, hostname, port);
  
    memset(&sock_in, 0, sizeof(struct sockaddr_in));
    memset(&thishostname, 0, MAXHOSTNAMELEN);
    if (strlen(hostname) == 0) {
        if (gethostname(thishostname, MAXHOSTNAMELEN) == 0) {
            log_msg("The hostname for this machine is %s.\n", thishostname);
        } else {
            log_msg("Couldn't get the hostname for this machine!\n");
            return false;
        }   
    }
    const struct hostent *hent = ::gethostbyname(hostname);
    if (hent > 0) {
        ::memcpy(&sock_in.sin_addr, hent->h_addr, hent->h_length);
    }
    sock_in.sin_family = AF_INET;
    sock_in.sin_port = ntohs(static_cast<short>(port));

#if 0
    char ascip[32];
    inet_ntop(AF_INET, &sock_in.sin_addr.s_addr, ascip, INET_ADDRSTRLEN);
    log_msg("The IP address for this client socket is %s\n", ascip);
#endif

    proto = ::getprotobyname("TCP");

    _sockfd = ::socket(PF_INET, SOCK_STREAM, proto->p_proto);
    if (_sockfd < 0)
        {
            log_error("unable to create socket : %s\n", strerror(errno));
            _sockfd = -1;
            return false;
        }

    retries = 2;
    while (retries-- > 0) {
        // We use select to wait for the read file descriptor to be
        // active, which means there is a client waiting to connect.
        FD_ZERO(&fdset);
        FD_SET(_sockfd, &fdset);
    
        // Reset the timeout value, since select modifies it on return. To
        // block, set the timeout to zero.
        tval.tv_sec = 5;
        tval.tv_usec = 0;
    
        ret = ::select(_sockfd+1, &fdset, NULL, NULL, &tval);

        // If interupted by a system call, try again
        if (ret == -1 && errno == EINTR)
            {
                log_msg("The connect() socket for fd #%d was interupted by a system call!\n",
                        _sockfd);
                continue;
            }
    
        if (ret == -1)
            {
                log_msg("The connect() socket for fd #%d never was available for writing!\n",
                        _sockfd);
#ifdef HAVE_WINSOCK_H
                ::shutdown(_sockfd, 0); // FIXME: was SHUT_BOTH
#else
                ::shutdown(_sockfd, SHUT_RDWR);
#endif
                _sockfd = -1;      
                return false;
            }
        if (ret == 0) {
            log_error("The connect() socket for fd #%d timed out waiting to write!\n",
                      _sockfd);
            continue;
        }

        if (ret > 0) {
            ret = ::connect(_sockfd, reinterpret_cast<struct sockaddr *>(&sock_in), sizeof(sock_in));
            if (ret == 0) {
                log_msg("\tport %d at IP %s for fd #%d\n", port,
                        ::inet_ntoa(sock_in.sin_addr), _sockfd);
                _connected = true;
                return true;
            }
            if (ret == -1) {
                log_msg("The connect() socket for fd #%d never was available for writing!\n",
                        _sockfd);
                _sockfd = -1;      
                return false;
            }
        }
    }
    //  ::close(_sockfd);
    //  return false;

    printf("\tConnected at port %d on IP %s for fd #%d\n", port,
           ::inet_ntoa(sock_in.sin_addr), _sockfd);
  
#ifndef HAVE_WINSOCK_H
    fcntl(_sockfd, F_SETFL, O_NONBLOCK);
#endif

    _connected = true;
    return true;
}

bool
Network::closeNet()
{  
//    log_msg("%s: \n", __PRETTY_FUNCTION__);
    if (_sockfd > 0) {
        closeNet(_sockfd);
        _sockfd = 0;
        _connected = false;        
    }
  
    return false;
}

bool
Network::closeNet(int sockfd)
{
    int retries = 0;
    
    // If we can't close the socket, other processes must be
    // locked on it, so we wait a second, and try again. After a
    // few tries, we give up, cause there must be something
    // wrong.

    if (sockfd <= 0) {
        return true;
    }
  
    while (retries < 3) {
        if (sockfd) {
            // Shutdown the socket connection
#if 0
            if (shutdown(sockfd, SHUT_RDWR) < 0) {
                if (errno != ENOTCONN) {
                    cerr << "WARNING: Unable to shutdown socket for fd #\n"
                         << sockfd << strerror(errno) << endl;
                } else {
                    cerr << "The socket using fd #" << sockfd
                         << " has been shut down successfully." << endl;
                    return true;
                }
            }
#endif 
            if (close(sockfd) < 0) {
                log_msg("WARNING: Unable to close the socket for fd%d\n%s\n",
                        sockfd, strerror(errno));
#ifndef HAVE_WINSOCK_H
                sleep(1);
#endif
                retries++;
            } else {
                log_msg("Closed the socket on fd #%d\n", sockfd);
                return true;
            }
        }
    } 
    return false;
}
// Description: Close an open socket connection.
bool
Network::closeConnection(void)
{
    log_msg("%s: \n", __FUNCTION__);

    closeConnection(_sockfd);
    _listenfd = 0;
    _connected = false;
  
    return false;
}

bool
Network::closeConnection(int fd)
{
    log_msg("%s: \n", __FUNCTION__);
    if (fd > 0) {
        closeConnection(fd);
    }
  
    return false;
}

void
Network::toggleDebug(bool val)
{
    // Turn on our own debugging
    _debug = val;

    // Turn on debugging for the utility methods
    toggleDebug(true);
}

#ifdef ENABLE_TESTING
// These are the callbacks used to define custom methods for our AS
// classes. This way we can examine the private dats after calling a
// method to see if it worked correctly.
void network_geturl(const fn_call& fn)
{

    network_as_object *ptr = (network_as_object*)fn.this_ptr;
    assert(ptr);
    fn.result->set_tu_string(ptr->obj.getURL().c_str());
}

void network_getprotocol(const fn_call& fn)
{
    network_as_object *ptr = (network_as_object*)fn.this_ptr;
    assert(ptr);
    fn.result->set_tu_string(ptr->obj.getProtocol().c_str());
}

void network_gethost(const fn_call& fn)
{
    network_as_object *ptr = (network_as_object*)fn.this_ptr;
    assert(ptr);
    fn.result->set_tu_string(ptr->obj.getHost().c_str());
}

void network_getport(const fn_call& fn)
{
    network_as_object *ptr = (network_as_object*)fn.this_ptr;
    assert(ptr);
    fn.result->set_tu_string(ptr->obj.getPortStr().c_str());
}

void network_getpath(const fn_call& fn)
{
    network_as_object *ptr = (network_as_object*)fn.this_ptr;
    assert(ptr);
    fn.result->set_tu_string(ptr->obj.getPath().c_str());
}

void network_connected(const fn_call& fn)
{
    network_as_object *ptr = (network_as_object*)fn.this_ptr;
    assert(ptr);
    fn.result->set_bool(ptr->obj.connected());
}
void network_getfilefd(const fn_call& fn)
{
    network_as_object *ptr = (network_as_object*)fn.this_ptr;
    assert(ptr);
    fn.result->set_int(ptr->obj.getFileFd());
}
void network_getlistenfd(const fn_call& fn)
{
    network_as_object *ptr = (network_as_object*)fn.this_ptr;
    assert(ptr);
    fn.result->set_int(ptr->obj.getListenFd());
}
#endif

} // end of gnash namespace
