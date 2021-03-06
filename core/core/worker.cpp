/****************************************************************************

Git <https://github.com/sniper00/MoonNetLua>
E-Mail <hanyongtao@live.com>
Copyright (c) 2015-2017 moon
Licensed under the MIT License <http://opensource.org/licenses/MIT>.

****************************************************************************/
#include "worker.h"
#include "common/time.hpp"
#include "common/string.hpp"
#include "service.h"
#include "message.hpp"
#include "log.h"
#include "server.h"


namespace moon
{
    worker::worker()
        :shared_(true)
        , exit_(false)
        , stoped_(false)
        , workerid_(0)
        , cache_uuid_(0)
        , serviceuid_(1)
        , start_time_(0)
        , work_time_(0)
        , server_(nullptr)
        , ios_(1)
        , work_(ios_)
    {
    }

    worker::~worker()
    {
    }

    void worker::run()
    {
        stoped_ = false;
        thread_ = std::thread([this]() {
            CONSOLE_INFO(server_->logger(),"WORKER-%d start", workerid_);
            start_time_ = time::millsecond();
            ios_.run();
            CONSOLE_INFO(server_->logger(), "WORKER-%d stop", workerid_);
        });
    }

    void worker::stop()
    {
        if (exit_)
        {
            return;
        }

        post([this] {
            if (services_.empty())
            {
                stoped_ = true;
                return;
            }
            exit_ = true;
            for (auto& it : services_)
            {
                auto& s = it.second;
                s->exit();
            }
        });
    }

    void worker::wait()
    {
        ios_.stop();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    bool worker::stoped()
    {
        return stoped_;
    }

    uint32_t worker::make_serviceid()
    {
        auto uid = serviceuid_.fetch_add(1);
        uid %= MAX_SERVICE_NUM;
        uint32_t tmp = uid+1;
        uint8_t wkid = workerid();
        tmp |= static_cast<uint32_t>(wkid) << WORKER_ID_SHIFT;
        return tmp;
    }

    void worker::add_service(const service_ptr_t & s)
    {
        post([this,s](){
            MOON_CHECK(services_.emplace(s->id(), s).second, "serviceid repeated");
            s->ok(true);
            servicenum_.store(static_cast<uint32_t>(services_.size()));
            CONSOLE_INFO(server_->logger(),"[WORKER %d] new service [%s:%u]", workerid(), s->name().data(), s->id());
        });    
    }

    void worker::remove_service(uint32_t id, uint32_t sender, uint32_t respid, bool crashed)
    {
        post([this, id,sender,respid,crashed]() {
            std::string response_content;
            auto iter = services_.find(id);
            if (services_.end() != iter)
            {
                auto& s = iter->second;
                s->destroy();
                if (services_.size() == 0)
                {
                    shared(true);
                }
                response_content = moon::format(R"({"name":"%s","serviceid":%u})",s->name().data(), s->id());    
                if (!crashed)
                {
                    on_service_remove(id);
                }
                servicenum_.store(static_cast<uint32_t>(services_.size()));
                server_->make_response(sender, "service destroy",response_content, respid);
                CONSOLE_INFO(server_->logger(), "[WORKER %d]service [%s:%u] destroy", workerid(), s->name().data(), s->id());
                services_.erase(iter);

                auto m = message::create();
                m->set_header("exit");
                m->set_type(PTYPE_SYSTEM);
                if (crashed)
                {
                    m->write_string("service crashed");
                }
                else
                {
                    m->write_string("service exit");
                }
                get_server()->broadcast(id, m);
            }
            else
            {
                static const char* errmsg = "remove_service:service not found";
                server_->make_response(sender, "error", errmsg, respid, PTYPE_ERROR);
            }

            if (services_.size()==0 &&  exit_)
            {
                stoped_ = true;
            }
        });
    }

    asio::io_service & worker::io_service()
    {
        return ios_;
    }

    uint32_t worker::make_cache(const buffer_ptr_t & buf)
    {
       auto iter = caches_.emplace(cache_uuid_++, buf);
       if (iter.second)
       {
           return iter.first->first;
       }
       return 0;
    }

    buffer_ptr_t worker::get_cache(uint32_t cacheid)
    {
        auto iter = caches_.find(cacheid);
        if (iter == caches_.end())
        {
            CONSOLE_DEBUG(server_->logger(), "send_cache failed, can not find cache data id %s", cacheid);
            return nullptr;
        }
        return iter->second;
    }

    void worker::send(const message_ptr_t & msg, bool immediately)
    {
        if (immediately)
        {
            post([this, msg]() {
                handle_one(nullptr, msg);
            });
        }
        else
        {
            mqueue_.push_back(msg);
        }
    }

    uint8_t worker::workerid() const
    {
        return workerid_;
    }

    void worker::workerid(uint8_t id)
    {
        workerid_ = id;
    }

    void worker::set_server(server * v)
    {
        server_ = v;
    }

    server* worker::get_server() const
    {
        return server_;
    }

    service * worker::find_service(uint32_t serviceid) const
    {
        auto iter = services_.find(serviceid);
        if (services_.end() != iter)
        {
            return iter->second.get();
        }
        return nullptr;
    }

    void worker::shared(bool v)
    {
        shared_ = v;
    }

    bool worker::shared() const
    {
        return shared_.load();
    }

    uint32_t worker::servicenum() const
    {
        return servicenum_.load();
    }

    void worker::start()
    {
        post([this] {
            for (auto& it : services_)
            {
                it.second->start();
            }
        });
    }

    void worker::update()
    {
        post([this] {
            auto begin_time = time::millsecond();

            for (auto& it : services_)
            {
                it.second->update();
            }

            if (mqueue_.size() != 0)
            {
                service* ser = nullptr;
                swapqueue_.clear();
                mqueue_.swap(swapqueue_);
                for (auto& msg : swapqueue_)
                {
                    handle_one(ser, msg);
                }
                if (cache_uuid_ != 0)
                {
                    cache_uuid_ = 0;
                    caches_.clear();
                }
            }
            auto difftime = time::millsecond() - begin_time;
            work_time_ += difftime;
        });
    }

    void worker::worker_time(uint32_t sender, uint32_t respid)
    {
        post([this, sender, respid]() {
            auto cur = time::millsecond();
            auto total_time  = cur - start_time_;
            total_time = total_time == 0 ? 1 : total_time;

            auto percent = static_cast<float>(work_time_) / static_cast<float>(total_time);
            auto response_content = moon::format(R"(["worker%d",%.2f])", workerid(), percent*100);
            server_->make_response(sender,"",response_content, respid);
            start_time_ = cur;
            work_time_ = 0;
        });
    }

    void worker::handle_one(service* ser,const message_ptr_t & msg)
    {
        if (msg->broadcast())
        {
            for (auto& it : services_)
            {
                auto& s = it.second;
                if (s->ok() && s->id() != msg->sender())
                {
                    s->handle_message(msg);
                }
            }
            return;
        }

        if (nullptr == ser || ser->id() != msg->receiver())
        {
            ser = find_service(msg->receiver());
            if (nullptr == ser)
            {
                server_->make_response(msg->sender(), "error", "call dead service.", msg->responseid(), PTYPE_ERROR);
                return;
            }
        }
        ser->handle_message(msg);
    }
}
