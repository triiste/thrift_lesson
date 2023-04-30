// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include "match_server/Match.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/ThreadFactory.h>
#include <thrift/transport/TTransportUtils.h>
#include "save_client/Save.h"
#include <thrift/TToString.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

#include<iostream>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<queue>
#include<vector>
#include<unistd.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace ::save_service;
using namespace ::match_service;
using namespace std; //多人合作时候最后不要加，容易产生冲突


struct Task{
    User user;
    string type;
};

struct MessageQueue{ //消息队列
    queue<Task> q;
    mutex m; //实现互斥
    condition_variable cv;//实现同步
}message_queue;

class Pool
{
    public:
        void save_result(int a,int b){
            printf("Match Result: %d %d\n",a,b);

            std::shared_ptr<TTransport> socket(new TSocket("123.57.47.211", 9090));
            std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
            std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
            SaveClient client(protocol);

            try {
                transport->open();

                client.save_data("acs_4666","526a1b86",a,b);

                transport->close();
            } catch (TException& tx) {
                cout << "ERROR: " << tx.what() << endl;
            }


        }
        void add(User user)
        {
            users.push_back(user);
            wt.push_back(0);
        }
        void remove(User user)
        {
            for(uint32_t i=0;i<users.size();i++)
            {
                if(users[i].id == user.id)
                {
                    users.erase(users.begin()+i);
                    wt.erase(wt.begin() + i);
                    break;
                }
            }
        }
        bool check_match(uint32_t i,uint32_t j){
            auto a=users[i],b=users[j];
            int dt = abs(a.score - b .score);
            int a_max_dif = wt[i] *50;
            int b_max_dif = wt[j] *50;
            return dt<=a_max_dif && dt<=b_max_dif;
        }

        void match(){
            for(uint32_t i=0;i<wt.size();i++)
                wt[i]++;  //等待秒数＋1
            while(users.size() > 1){
                bool flag = true;
                for(uint32_t i = 0; i<users.size();i++)
                {
                    for(uint32_t j=i+1;j<users.size();j++){
                        if(check_match(i,j)){
                            auto a=users[i],b=users[j];
                            //先删掉后面的再删掉前面的
                            users.erase(users.begin()+j);
                            users.erase(users.begin()+i);
                            //先删掉后面再删掉前面
                            wt.erase(wt.begin()+j);
                            wt.erase(wt.begin()+i);
                            save_result(a.id,b.id);
                            flag=false;
                            break;
                        }
                    }
                    if(!flag) break;
                }
                if(flag) break;

            }
        }

    private:
        vector<User> users;
        vector<int> wt;//表示已经等待的秒数,等待时间，单位：s
}pool;


class MatchHandler : virtual public MatchIf {
    public:
        MatchHandler() {
            // Your initialization goes here
        }
        //add和remove互斥访问
        int32_t add_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("add_user\n");

            unique_lock<mutex> lck(message_queue.m);//用锁把它锁起来，变量作用域消失会自动解锁
            message_queue.q.push({user,"add"});
            //唤醒锁
            message_queue.cv.notify_all();
            return 0;
        }

        int32_t remove_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("remove_user\n");

            unique_lock<mutex> lck(message_queue.m);
            message_queue.q.push({user,"remove"});
            message_queue.cv.notify_all(); //唤醒阻塞线程
            return 0;
        }

};
class MatchCloneFactory : virtual public MatchIfFactory {
    public:
        ~MatchCloneFactory() override = default;
        MatchIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo) override
        {
            std::shared_ptr<TSocket> sock = std::dynamic_pointer_cast<TSocket>(connInfo.transport);
/*            cout << "Incoming connection\n";
            cout << "\tSocketInfo: "  << sock->getSocketInfo() << "\n";
            cout << "\tPeerHost: "    << sock->getPeerHost() << "\n";
            cout << "\tPeerAddress: " << sock->getPeerAddress() << "\n";
            cout << "\tPeerPort: "    << sock->getPeerPort() << "\n";*/
            return new MatchHandler;
        }
        void releaseHandler(MatchIf* handler) override {
            delete handler;
        }
};
//生产者-消费者模型
void consume_task(){
    //对队列操作是共享的，操作完队列就解锁
    while(true)
    {   //队列存的是任务,不是user
        unique_lock<mutex> lck(message_queue.m);
        if(message_queue.q.empty()){
            //游戏刚上线大概率没人，如果没人来 应该把线程阻塞住，直到有新的玩家来在执行
            //message_queue.cv.wait(lck); //先将锁释放掉，然后卡住 
            lck.unlock();
            pool.match();
            sleep(1);//每1秒钟匹配1次
        }
        else
        {
            auto task = message_queue.q.front();
            message_queue.q.pop();
            lck.unlock();   //操作完记得解锁
            //do task
            //玩家池
            if(task.type == "add") pool.add(task.user);
            else if(task.type == "remove") pool.remove(task.user);
        }
    }
}

int main(int argc, char **argv) {

    TThreadedServer server(
            std::make_shared<MatchProcessorFactory>(std::make_shared<MatchCloneFactory>()),
            std::make_shared<TServerSocket>(9090), //port
            std::make_shared<TBufferedTransportFactory>(),
            std::make_shared<TBinaryProtocolFactory>());


    cout<<"Start Match Server"<<endl;
    //多线程 一堆消费者的线程和一堆生产者的线程
    //生产者和消费者之间需要通信的媒介
    //c++实现的消息队列(自己实现一个消息队列)
    //信号量机制 锁有两个操作 一个p操作争取锁  一个v操作放开锁(保证同时只有一个进程写这个队列)
    //条件变量 对锁进行了一个封装
    thread matching_thread(consume_task); //一个线程
    server.serve();//一个线程
    return 0;
}

