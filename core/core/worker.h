/****************************************************************************

Git <https://github.com/sniper00/MoonNetLua>
E-Mail <hanyongtao@live.com>
Copyright (c) 2015-2017 moon
Licensed under the MIT License <http://opensource.org/licenses/MIT>.

****************************************************************************/

#pragma once
#include "config.h"
#include "asio.hpp"
#include "common/concurrent_queue.hpp"
#include "common/spinlock.hpp"

namespace moon
{
    class server;

    class worker
    {
    public:
        static const uint16_t MAX_SERVICE_NUM = 0xFFFF;

        friend class server;

        worker();

        ~worker();

        worker(const worker&) = delete;

        worker& operator=(const worker&) = delete;

        server* get_server() const;

        void remove_service(uint32_t serviceid, uint32_t sender, uint32_t respid, bool crashed = false);

        asio::io_service& io_service();

        uint32_t make_cache(const buffer_ptr_t & buf);

        buffer_ptr_t  get_cache(uint32_t cacheid);
    private:
        void run();

        void stop();

        void wait();

        bool stoped();

        template<typename THandler>
        void post(THandler&& h)
        {
            ios_.post(std::forward<THandler>(h));
        }

        uint32_t make_serviceid();

        void add_service(const service_ptr_t& s);

        void send(const message_ptr_t& msg,bool immediately =false);
    
        uint8_t workerid() const;

        void workerid(uint8_t id);

        void set_server(server* v);

        void shared(bool v);

        bool shared() const;

        service* find_service(uint32_t serviceid) const;

        uint32_t servicenum() const;

        std::function<void(uint32_t)> on_service_remove;
    private:
        void start();

        void update();

        void worker_time(uint32_t sender, uint32_t respid);

        void handle_one(service* ser,const message_ptr_t& msg);
    private:
        std::atomic_bool shared_;
        bool exit_;
        std::atomic_bool stoped_;
        uint8_t workerid_;
        uint32_t cache_uuid_;
        std::atomic<uint16_t> serviceuid_;
        std::atomic<uint32_t> servicenum_;

        int64_t start_time_;
        int64_t work_time_;
        server*  server_;
        std::thread thread_;
        asio::io_service ios_;
        asio::io_service::work work_;
        std::unordered_map<uint32_t, service_ptr_t> services_;
        std::vector<message_ptr_t> swapqueue_;
        sync_queue<message_ptr_t, moon::spin_lock> mqueue_;
        std::unordered_map<uint32_t, buffer_ptr_t> caches_;
    };
};


