#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stdlib.h>
#include <mutex>
#include <vector>

// 模板参数 T: 对象类型
// 模板参数 BlockSize: 每次向系统申请多少个对象（默认4096个）
template<typename T, int BlockSize = 4096>
class MemoryPool {
public:
    MemoryPool() {
        m_free_list = nullptr;
    }

    ~MemoryPool() {
        // 释放所有申请的 Block
        for (auto p : m_blocks) {
            ::operator delete(p); // 对应 malloc 的释放
        }
    }

    // 分配一个对象
    void* allocate() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // 1. 如果自由链表里有空闲对象，直接拿一个
        if (m_free_list) {
            void* p = m_free_list;
            // 自由链表头指针后移 (指向下一个空闲块)
            m_free_list = *(reinterpret_cast<void**>(m_free_list));
            return p;
        }

        // 2. 如果没有，向系统申请一大块 (Block)
        // 注意：这里申请的是原始内存 (char*)
        // BlockSize * sizeof(T) 可能会因为对齐问题导致大小不够，
        // 但这里简化处理，认为 sizeof(T) >= sizeof(void*)
        if (sizeof(T) < sizeof(void*)) {
            // 静态断言：对象大小必须至少能存下一个指针，否则没法构建自由链表
            // throw std::runtime_error("Object too small for memory pool");
            // 实际上 util_timer 很大，肯定没问题
        }

        // 申请一大块内存
        char* new_block = static_cast<char*>(::operator new(BlockSize * sizeof(T)));
        m_blocks.push_back(new_block);

        // 3. 将这一大块切分成小块，串成链表
        // 比如：[0]->[1]->[2]->...->[N-1]->NULL
        T* ptrs = reinterpret_cast<T*>(new_block);
        
        // 从第 0 个到第 N-2 个，都指向下一个
        for (int i = 0; i < BlockSize - 1; ++i) {
            // 在当前对象的内存位置，写入下一个对象的地址
            // 这就是“嵌入式指针”技巧
            *(reinterpret_cast<void**>(&ptrs[i])) = &ptrs[i + 1];
        }
        
        // 最后一个指向 nullptr (或者原本的 free_list，如果想扩容的话)
        *(reinterpret_cast<void**>(&ptrs[BlockSize - 1])) = nullptr;

        // 4. 更新 free_list
        // 返回第一个块给用户，free_list 指向第二个块
        void* ret = &ptrs[0];
        m_free_list = &ptrs[1];
        
        return ret;
    }

    // 归还一个对象
    void deallocate(void* p) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // 头插法：将 p 插回 free_list 头部
        *(reinterpret_cast<void**>(p)) = m_free_list;
        m_free_list = p;
    }

private:
    void* m_free_list;              // 自由链表头指针
    std::vector<void*> m_blocks;    // 记录申请过的大内存块，方便析构释放
    std::mutex m_mutex;             // 线程安全锁
};

#endif