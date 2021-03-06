#include "f2f.h"

#include "f2f_ss.hpp"

#include "playdar/resolver_query.hpp"
#include "playdar/playdar_request.h"

using namespace std;
using namespace json_spirit;

namespace playdar {
namespace resolvers {

bool
f2f::init(pa_ptr pap)
{
    m_pap = pap;
    string jid = m_pap->get<string>("plugins.f2f.jid","");
    string pass= m_pap->get<string>("plugins.f2f.pass","");
    if( jid.empty() || pass.empty() )
    {
        cerr << "\n\nf2f plugin not initializing - no jid+pass in config file.\n" << endl;
        return false;
    }
    // start xmpp connection in new thread:
    m_jbot = boost::shared_ptr<jbot>(new jbot(jid, pass));
    m_jbot->set_msg_received_callback(boost::bind(&f2f::msg_received, this, _1, _2));
    m_jbot_thread = boost::shared_ptr<boost::thread>
                        (new boost::thread(boost::bind(&jbot::start, m_jbot)));
    
    m_io = boost::shared_ptr< boost::asio::io_service >
                            (new boost::asio::io_service);
    m_work = boost::shared_ptr< boost::asio::io_service::work>
                            (new boost::asio::io_service::work(*m_io));
    for (std::size_t i = 0; i < 1/*thread count*/; ++i)
    {
        m_io_threads.create_thread(boost::bind(&boost::asio::io_service::run, m_io));
    }
    return true;
}

f2f::~f2f() throw()
{
    m_io->stop();
    m_jbot->stop();
    m_jbot_thread->join();
    m_io_threads.join_all();
}

void
f2f::msg_received( const string& msg, const string& from )
{
    cout << "Msg rcvd to f2f: " << msg << endl;
    using namespace json_spirit;
    Value mv;
    if( !read( msg, mv ) )
    {
        cout << "Discarding msg, not valid JSON" << endl;
        return;
    }
    Object qo = mv.get_obj();
    map<string,Value> r;
    obj_to_map(qo,r);
    // we identify JSON messages by the "_msgtype" property:
    string msgtype = "";
    if( r.find("_msgtype")!=r.end() && r["_msgtype"].type() == str_type )
    {
        msgtype = r["_msgtype"].get_str();
    }
    else
    {
        cerr << "JSON msg rcvd without _msgtype - discarding" << endl;
        return;
    }
    // Handle different _msgtypes (only query and response for now)
    if(msgtype == "rq") // REQUEST / NEW QUERY
    {
        cout << "DISPATCHING from f2f" << endl;
        boost::shared_ptr<ResolverQuery> rq;
        try {
            rq = ResolverQuery::from_json(qo);
        }        
        catch (...)
        {
            cout << "f2f: Missing fields in query json, discarding" << endl;
            return;
        }
        if( m_pap->query_exists(rq->id()) ) return; // QID already exists
        // dispatch query with our callback that will
        // respond to the searcher via an XMPP msg.
        rq_callback_t cb = boost::bind(&f2f::send_response, this, _1, _2, from);
        query_uid qid = m_pap->dispatch( rq, cb );
    }
    else if(msgtype == "result") // RESPONSE 
    {
        Object resobj = r["result"].get_obj();
        map<string,Value> resobj_map;
        obj_to_map(resobj, resobj_map);
        query_uid qid = r["qid"].get_str();
        if(!m_pap->query_exists(qid)) return; // QID invalid/expired
        
        vector< Object > final_results;
        try
        {
            ResolvedItem ri(resobj);
            ostringstream osurl;
            osurl << "f2f://" << from << "#" << ri.id(); //TODO no SS for this yet ;)
            ri.set_url( osurl.str() );
            final_results.push_back( ri.get_json() );
            m_pap->report_results( qid, final_results );
        }
        catch (...)
        {
            cout << "f2f: Missing fields in response json, discarding" << endl;
            return;
        }
    }
}

/// fired when a new result is available for a running query:
void
f2f::send_response( query_uid qid, ri_ptr rip, const string& from )
{
    using namespace json_spirit;
    Object response;
    response.push_back( Pair("_msgtype", "result") );
    response.push_back( Pair("qid", qid) );
    ostringstream osurl;
    osurl << "f2f://" << from << "#" << rip->id(); //TODO no SS for this yet ;)
    rip->set_url( osurl.str() );
    Object result = rip->get_json();
    response.push_back( Pair("result", result) );
    ostringstream ss;
    write( response, ss );
    // send response to the person the query came from as a chat msg:
    m_jbot->send_to( from, ss.str() );
    //cout << "REPLY -> " << from << " -> " << ss.str() << endl;
}

/// ran in a thread to dispatch msgs
void
f2f::process( rq_ptr rq )
{
    try
    {
        if(rq && !rq->cancelled())
        {
            ostringstream os;
            json_spirit::write( rq->get_json(), os );
            m_jbot->broadcast_msg( os.str() );
        }
    }
    catch(...)
    {
        cout << "f2f: error processing an RQ" << endl;
    }
}

void
f2f::start_resolving(boost::shared_ptr<ResolverQuery> rq)
{
    m_io->post( boost::bind(&f2f::process,this,rq) );
}

void 
f2f::cancel_query(query_uid qid)
{}

/// return SS facts, ie our f2f xmpp one.
map< std::string, boost::function<ss_ptr(std::string)> > 
f2f::get_ss_factories()
{
    map< std::string, boost::function<ss_ptr(std::string)> > facts;
    facts["f2f"] = boost::bind(&f2fStreamingStrategy::factory, _1, m_jbot);
    return facts;
}


bool
f2f::anon_http_handler(const playdar_request& req, playdar_response& resp)
{
    cout << "request handler on f2f for url: " << req.url() << endl;
    ostringstream os;
    os <<   "<h2>f2f info</h2>"
            "todo"
            ;
    resp = playdar_response(os.str(), true);
    return true;
}

}} // namespaces
