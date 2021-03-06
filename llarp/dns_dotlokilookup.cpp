#include <dns_dotlokilookup.hpp>
#include <handlers/tun.hpp>
#include <logic.hpp>
#include <service/context.hpp>

std::string const default_chars =
    "abcdefghijklmnaoqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";

#include <random>

std::string
random_string(size_t len = 15, std::string const &allowed_chars = default_chars)
{
  std::mt19937_64 gen{std::random_device()()};

  std::uniform_int_distribution< size_t > dist{0, allowed_chars.length() - 1};

  std::string ret;

  std::generate_n(std::back_inserter(ret), len,
                  [&] { return allowed_chars[dist(gen)]; });
  return ret;
}

struct check_query_simple_request
{
  // already inside request
  // const struct sockaddr *from;  // source
  const dnsd_question_request *request;
};

std::unordered_map< std::string, struct dnsd_query_hook_response * >
    loki_tld_lookup_cache;

static bool
decode_request_name(const std::string &name, llarp::AlignedBuffer< 32 > &addr,
                    bool &isSNode)
{
  llarp::service::Address serviceAddr;
  llarp::RouterID snodeAddr;
  auto pos = name.find(".snode");
  if(pos != std::string::npos)
  {
    if(!llarp::HexDecode(name.substr(0, pos).c_str(), serviceAddr.data().data(),
                         serviceAddr.size()))
    {
      return false;
    }
    addr    = snodeAddr.data();
    isSNode = true;
  }
  else
  {
    if(!serviceAddr.FromString(name))
    {
      return false;
    }
    addr    = serviceAddr.data().data();
    isSNode = false;
  }
  return true;
}

void
llarp_dotlokilookup_checkQuery(void *u, __attribute__((unused)) uint64_t orig,
                               uint64_t left)
{
  if(left)
    return;
  // struct check_query_request *request = static_cast< struct
  // check_query_request * >(u);
  struct check_query_simple_request *qr =
      static_cast< struct check_query_simple_request * >(u);
  dotLokiLookup *dll = (dotLokiLookup *)qr->request->context->user;
  if(!dll)
  {
    llarp::LogError("DNSd dotLokiLookup is not configured");
    write404_dnss_response(qr->request);
    delete qr;
    return;
  }

  // we do have result
  // if so send that
  // else
  // if we have a free private ip, send that

  /*
  // cache hit
  auto itr = loki_tld_lookup_cache.find(addr.ToString());
  if(itr != loki_tld_lookup_cache.end())
  {
    llarp::LogDebug("Found in .loki lookup cache");
    writesend_dnss_response(itr->second->returnThis, qr->from, qr->request);
    delete qr;
    return;
  }

  struct dns_pointer *free_private = dns_iptracker_get_free(dll->ip_tracker);
  if(free_private)
  {
    */

  // in_addr ip_address = ((sockaddr_in *)free_private->hostResult)->sin_addr;

  llarp::service::Context *routerHiddenServiceContext =
      (llarp::service::Context *)dll->user;
  if(!routerHiddenServiceContext)
  {
    llarp::LogWarn("dotLokiLookup user isnt a service::Context: ", dll->user);
    write404_dnss_response(qr->request);
    delete qr;
    return;
  }
  llarp::huint32_t serviceIP;
  llarp::AlignedBuffer< 32 > addr;
  bool isSNode = false;
  if(!decode_request_name(qr->request->question.name, addr, isSNode))
  {
    llarp::LogWarn("decode_request_name failed");
    write404_dnss_response(qr->request);
    delete qr;
    return;
  }
  if(!routerHiddenServiceContext->FindBestAddressFor(addr, isSNode, serviceIP))
  {
    llarp::LogWarn("dotLokiLookup failed to map address");
    write404_dnss_response(qr->request);
    delete qr;
    return;
  }

  /*
  bool mapResult = routerHiddenServiceContext->MapAddressAll(
      addr, free_private->hostResult);
  if(!mapResult)
  {
    llarp::LogWarn("dotLokiLookup failed to map address");
    write404_dnss_response(qr->from, qr->request);
    delete qr;
    return;
  }
  */

  // make a dnsd_query_hook_response for the cache
  dnsd_query_hook_response *response = new dnsd_query_hook_response;
  response->dontLookUp               = true;
  response->dontSendResponse         = false;
  // llarp::Addr test(*free_private->hostResult.getSockAddr());
  // llarp::LogInfo("IP Test: ", test);
  // response->returnThis = &free_private->hostResult;
  response->returnThis = serviceIP;
  llarp::LogInfo("Saving ", qr->request->question.name);
  loki_tld_lookup_cache[qr->request->question.name] = response;
  // we can't delete response now...

  /*
  llarp::handlers::TunEndpoint *tunEndpoint =
      (llarp::handlers::TunEndpoint *)dll->user;
  if (!tunEndpoint)
  {
    llarp::LogWarn("dotLokiLookup user isnt a tunEndpoint: ", dll->user);
    return;
  }
  bool mapResult = tunEndpoint->MapAddress(addr,
  free_private->hostResult.tohl()); if(!mapResult)
  {
    delete qr;
    return;
  }
  */
  llarp::huint32_t foundAddr;
  if(!routerHiddenServiceContext->FindBestAddressFor(addr, isSNode, foundAddr))
  {
    write404_dnss_response(qr->request);
    delete qr;
    return;
  }

  // make a dnsd_query_hook_response for the cache
  /*
  dnsd_query_hook_response *response = new dnsd_query_hook_response;
  response->dontLookUp               = true;
  response->dontSendResponse         = false;
  loki_tld_lookup_cache[addr.ToString()]=response;
   */
  // we can't delete response now...
  // sockaddr_in saddr;
  // saddr.sin_family      = AF_INET;
  // saddr.sin_addr.s_addr = llarp::xhtonl(foundAddr).n;
  // FIXME: flush cache to disk
  // on crash we'll need to bring up all the same IPs we assigned before...
  writesend_dnss_response(&foundAddr, qr->request);
  delete qr;
  return;
}

std::vector< std::string >
split(std::string str)
{
  size_t pos = 0;
  std::string token;
  std::string s(str);
  std::vector< std::string > tokens;
  while((pos = s.find(".")) != std::string::npos)
  {
    token = s.substr(0, pos);
    // llarp::LogInfo("token [", token, "]");
    tokens.push_back(token);
    s.erase(0, pos + 1);
  }
  token = s.substr(0, pos);
  tokens.push_back(token);
  // llarp::LogInfo("token [", token, "]");
  return tokens;
}

struct reverse_handler_iter_context
{
  std::string lName;
  // const struct sockaddr *from; // aready inside dnsd_question_request
  const dnsd_question_request *request;
};

#if defined(ANDROID) || defined(RPI)
static int
stoi(const std::string &s)
{
  return atoi(s.c_str());
}
#endif

bool
ReverseHandlerIter(struct llarp::service::Context::endpoint_iter *endpointCfg)
{
  reverse_handler_iter_context *context =
      (reverse_handler_iter_context *)endpointCfg->user;
  // llarp::LogInfo("context ", context->request->question.name);
  // llarp::LogInfo("Checking ", lName);
  llarp::handlers::TunEndpoint *tunEndpoint =
      (llarp::handlers::TunEndpoint *)endpointCfg->endpoint;
  if(!tunEndpoint)
  {
    llarp::LogError("No tunnel endpoint found");
    return true;  // still continue
  }
  // llarp::LogInfo("for ", tunEndpoint->tunif.ifaddr);
  std::string checkStr(tunEndpoint->tunif.ifaddr);
  std::vector< std::string > tokensSearch = split(context->lName);
  std::vector< std::string > tokensCheck  = split(checkStr);

  // well the tunif is just one ip on a network range...
  // support "b._dns-sd._udp.0.0.200.10.in-addr.arpa"
  size_t searchTokens = tokensSearch.size();
  // if the query has five or fewer levels,
  // tack on leading '0.'s to form a minimum six-level
  // PTR query -rick
  if(searchTokens < 6)
  {
    switch(searchTokens)
    {
      case 5:
        tokensSearch.clear();
        context->lName.insert(0, "0.");
        tokensSearch = split(context->lName);
        searchTokens = tokensSearch.size();
        break;
      case 4:
        tokensSearch.clear();
        context->lName.insert(0, "0.0.");
        tokensSearch = split(context->lName);
        searchTokens = tokensSearch.size();
        break;
      case 3:
        tokensSearch.clear();
        context->lName.insert(0, "0.0.0.");
        tokensSearch = split(context->lName);
        searchTokens = tokensSearch.size();
        break;
      default:
        llarp::LogError("invalid PTR query: ", context->lName);
        break;
    }
  }
  // this expression assumes a six-level name
  std::string searchIp = tokensSearch[searchTokens - 3] + "."
      + tokensSearch[searchTokens - 4] + "." + tokensSearch[searchTokens - 5]
      + "." + tokensSearch[searchTokens - 6];
  std::string checkIp = tokensCheck[0] + "." + tokensCheck[1] + "."
      + tokensCheck[2] + "." + tokensCheck[3];
  llarp::LogDebug(searchIp, " vs ", checkIp);

  llarp::IPRange range =
      llarp::iprange_ipv4(std::stoi(tokensCheck[0]), std::stoi(tokensCheck[1]),
                          std::stoi(tokensCheck[2]), std::stoi(tokensCheck[3]),
                          tunEndpoint->tunif.netmask);  // create range

  llarp::huint32_t searchIPv4 = llarp::ipaddr_ipv4_bits(
      std::stoi(tokensSearch[searchTokens - 3]),
      std::stoi(tokensSearch[searchTokens - 4]),
      std::stoi(tokensSearch[searchTokens - 5]),
      std::stoi(tokensSearch[searchTokens - 6]));  // create ip

  // bool inRange = range.Contains(searchAddr.xtohl());
  bool inRange = range.Contains(searchIPv4);

  llarp::Addr searchAddr(searchIp);
  llarp::Addr checkAddr(checkIp);
  llarp::LogInfo(searchIPv4, " vs ", range, " = ",
                 inRange ? "inRange" : "not match");

  if(inRange)
  {
    llarp::AlignedBuffer< 32 > addr =
        tunEndpoint->ObtainAddrForIP< llarp::AlignedBuffer< 32 > >(searchIPv4,
                                                                   false);
    if(addr.IsZero())
    {
      addr = tunEndpoint->ObtainAddrForIP< llarp::AlignedBuffer< 32 > >(
          searchIPv4, true);
      if(!addr.IsZero())
      {
        char stack[128]   = {0};
        std::string saddr = llarp::HexEncode(addr, stack);
        saddr += ".snode";
        writesend_dnss_revresponse(saddr, context->request);
      }
      else
        write404_dnss_response(context->request);
    }
    else
    {
      llarp::service::Address saddr = addr.data();
      // llarp::LogInfo("Returning [", saddr.ToString(), "]");
      writesend_dnss_revresponse(saddr.ToString(), context->request);
    }
    return false;
  }
  return true;  // we don't do anything with the result yet
}

static bool
should_intercept_query_with_name(const std::string &lName)
{
  // null terminated list
  static const char *const matches[] = {".loki", ".snode", ".loki.", ".snode.",
                                        0};
  size_t idx                         = 0;
  while(matches[idx])
  {
    std::string match_str(matches[idx]);
    if(lName.substr(lName.length() - match_str.length()) == match_str)
      return true;
    ++idx;
  }
  return false;
}

dnsd_query_hook_response *
llarp_dotlokilookup_handler(std::string name,
                            const dnsd_question_request *request)
{
  dnsd_query_hook_response *response = new dnsd_query_hook_response;
  response->dontLookUp               = false;
  response->dontSendResponse         = false;
  response->returnThis.h             = 0;
  llarp::LogDebug("Hooked ", name);
  std::string lName = name;
  std::transform(lName.begin(), lName.end(), lName.begin(), ::tolower);
  // llarp::LogDebug("Transformed ", lName);

  // 253.0.200.10.in-addr.arpa
  if(lName.find(".in-addr.arpa") != std::string::npos)
  {
    // llarp::LogDebug("Checking ", lName);
    dotLokiLookup *dll = (dotLokiLookup *)request->context->user;
    llarp::service::Context *routerHiddenServiceContext =
        (llarp::service::Context *)dll->user;
    if(!routerHiddenServiceContext)
    {
      llarp::LogWarn("dotLokiLookup user isnt a service::Context: ", dll->user);
      return response;
    }
    // llarp::LogDebug("Starting rev iter for ", lName);
    // which range?
    // for each tun interface
    struct reverse_handler_iter_context context;
    context.lName = lName;
    // context.from    = request->from;
    context.request = request;

    struct llarp::service::Context::endpoint_iter i;
    i.user   = &context;
    i.index  = 0;
    i.visit  = &ReverseHandlerIter;
    bool res = routerHiddenServiceContext->iterate(i);
    if(!res)
    {
      llarp::LogDebug("Reverse is ours");
      response->dontLookUp       = true;
      response->dontSendResponse = true;  // should have already sent it
    }
    else
    {
      llarp::LogInfo("Reverse is not ours");
    }
  }
  else if(should_intercept_query_with_name(lName))
  {
    llarp::LogInfo("Detect Loki Lookup for ", lName);
    auto cache_check = loki_tld_lookup_cache.find(lName);
    if(cache_check != loki_tld_lookup_cache.end())
    {
      // was in cache
      llarp::LogInfo("Reused address from LokiLookupCache");
      // FIXME: avoid the response allocation if you could
      delete response;
      return cache_check->second;
    }

    // decode address
    llarp::AlignedBuffer< 32 > addr;
    bool isSNode = false;
    if(!decode_request_name(lName, addr, isSNode))
    {
      response->dontLookUp = true;
      return response;
    }

    dotLokiLookup *dll = (dotLokiLookup *)request->context->user;
    llarp::service::Context *routerHiddenServiceContext =
        (llarp::service::Context *)dll->user;
    if(!routerHiddenServiceContext)
    {
      llarp::LogWarn("dotLokiLookup user isnt a service::Context: ", dll->user);
      return response;
    }

    // start path build early (if you're looking it up, you're probably going to
    // use it)
    // main_router_prefetch(ctx, addr);

    // schedule future response
    check_query_simple_request *qr = new check_query_simple_request;
    // qr->from                       = request->from;
    qr->request = request;

    auto tun = routerHiddenServiceContext->getFirstTun();
    if(isSNode)
    {
      if(tun->HasPathToSNode(addr.data()))
      {
        llarp_dotlokilookup_checkQuery(qr, 0, 0);
        response->dontSendResponse = true;  // will send it shortly
        return response;
      }
    }
    else
    {
      if(tun->HasPathToService(addr.data()))
      {
        llarp_dotlokilookup_checkQuery(qr, 0, 0);
        response->dontSendResponse = true;  // will send it shortly
        return response;
      }
    }

    // nslookup on osx is about 5 sec before a retry, 2s on linux
    request->context->client.logic->call_later(
        {2000, qr, &llarp_dotlokilookup_checkQuery});

    response->dontSendResponse = true;  // will send it shortly
  }
  return response;
}
