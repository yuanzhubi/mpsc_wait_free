/*
 * mcsp
 *
 *  Created on: 2012-09-19
 *      Author: yuanzhu
 */

#ifndef __MCPS_H_
#define __MCPS_H_

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdexcept>

class mpsc_multiqueue;

class mpsc_queue {
	friend class mpsc_multiqueue;
private:
    mpsc_queue(const mpsc_queue&);
protected:
    volatile uint32_t m_data_index;              //64 bytes aligned cache line 0
    const uint32_t m_max_data_index;
	const uint32_t m_max_data_blocks_moder;
    char* const m_data;
    volatile uint32_t* const m_blocks;	
	char padding_1[64 - 4*3 - sizeof(void*)*2];	
	uint32_t m_pos_consumer_index;               //64 bytes aligned cache line 1
	char padding_2[64 - 4];	
    volatile uint32_t m_pos_producer_index;      //64 bytes aligned cache line 2

public:
    //Suppose each produce costs maximum x bytes. You need to gurantee x<<max_data_blocks_log_level >= max_data_bytes.
    mpsc_queue(uint32_t max_data_bytes, uint32_t max_data_blocks_log_level = 16 ):
        m_data_index(0), m_max_data_index(max_data_bytes),
        m_max_data_blocks_moder((2<<max_data_blocks_log_level) - 1),
        m_data((char*)calloc(max_data_bytes, 1)),
        m_blocks((volatile uint32_t*)calloc(2<<max_data_blocks_log_level, 4)), 
		m_pos_consumer_index(0), m_pos_producer_index(0){
    }
    ~mpsc_queue(){
        free((void*)m_blocks);
        free(m_data);
    }
	void clear(char c){
		memset(m_data, c, m_max_data_index);
	}
    //produce_func is a functor or function poitner used to copy data to the container
    template<typename F>
    bool produce(F& produce_func, uint32_t data_length){									//1. cross cores cache line sync
		uint32_t tmp_data_index = this->m_data_index;
		uint32_t rest_capacity = this->m_max_data_index - tmp_data_index;
        if(rest_capacity < data_length || tmp_data_index > this->m_max_data_index){ 		//not enough or waiting roll-back
            return false;
        }
        const uint32_t data_pos = __sync_fetch_and_add(&this->m_data_index, data_length);   //2. memory try-write
        if( data_pos + data_length > this->m_max_data_index || data_pos < tmp_data_index){  //We can not detect the congestion with size over 4GB.                
            __sync_sub_and_fetch(&this->m_data_index, data_length);                         //3. failed, roll-back.
            return false;
        }
		produce_func(this->m_data + data_pos, data_length);
		
        uint32_t pos_index = __sync_fetch_and_add(&this->m_pos_producer_index, 2) & (this->m_max_data_blocks_moder);       
        if(this->m_blocks[pos_index] != 0){
            throw std::out_of_range("The max_data_blocks_log_level is not enough!");
            return false;
        }
		
        this->m_blocks[pos_index + 1] = data_pos;
		//Weaker memory model needs explicit memory barrior instruction here.
        this->m_blocks[pos_index] = data_length;
        return true;
    }

    template<typename F>
    uint32_t consume(F& consume_func){
        uint32_t resolved_block = 0;
        uint32_t resolved_rest_data = this->m_max_data_index;
        uint32_t rest_capacity = 0;
        uint32_t tmp_pos_consumer_index = this->m_pos_consumer_index;
        do{
            do{
                for(uint32_t data_length;(data_length = this->m_blocks[tmp_pos_consumer_index]) != 0; ++resolved_block){
                    uint32_t data_pos = this->m_blocks[tmp_pos_consumer_index + 1];
                    resolved_rest_data -= data_length;
                    this->m_blocks[tmp_pos_consumer_index] = 0;
                    tmp_pos_consumer_index = (tmp_pos_consumer_index + 2) & (this->m_max_data_blocks_moder);					
					consume_func(this->m_data + data_pos, data_length);
                }
                rest_capacity = this->m_max_data_index - this->m_data_index;                                        //1. cross cores cache line sync
            }while(rest_capacity < resolved_rest_data);
            while(rest_capacity > resolved_rest_data){
                rest_capacity = this->m_max_data_index - this->m_data_index;                                           //2. waiting producers roll back
            }
        }while(rest_capacity < resolved_rest_data ||                 							//3. after roll back, the producers may produce again
		//Maybe rest_capacity == resolved_rest_data, every produced consumed? Try to release the used space!
			!__sync_bool_compare_and_swap(&this->m_data_index, this->m_max_data_index - resolved_rest_data, 0 )); 		//4. memory try-write
		
		//Weaker memory model needs explicit memory barrior instruction here.
        this->m_pos_consumer_index = tmp_pos_consumer_index;
        return resolved_block;
    }
	
	// Aggeragated consume, which means the argument of consume_func is aggregation of several production.
	template<typename F>
    uint32_t consume_agg(F& consume_func){ 
        uint32_t resolved_block = 0;
        uint32_t resolved_rest_data = this->m_max_data_index;
        uint32_t rest_capacity = 0;
        uint32_t tmp_pos_consumer_index = this->m_pos_consumer_index;
		uint32_t consumed_data = 0;
        do{
            do{
                for(uint32_t data_length ;(data_length = this->m_blocks[tmp_pos_consumer_index]) != 0; ++resolved_block){
                    resolved_rest_data -= data_length;
                    this->m_blocks[tmp_pos_consumer_index] = 0;
                    tmp_pos_consumer_index = (tmp_pos_consumer_index + 2) & (this->m_max_data_blocks_moder);					
                }
                rest_capacity = this->m_max_data_index - this->m_data_index;                                        //1. cross cores cache line sync
            }while(rest_capacity < resolved_rest_data);
            while(rest_capacity > resolved_rest_data){
                rest_capacity = this->m_max_data_index - this->m_data_index;                                        //2. waiting producers roll back
            }
        }while(rest_capacity < resolved_rest_data ||                 												//3. after roll back, the producers may produce again
			((this->m_max_data_index - resolved_rest_data != consumed_data) &&
				(consume_func(this->m_data + consumed_data, this->m_max_data_index - resolved_rest_data - consumed_data),
					consumed_data = this->m_max_data_index - resolved_rest_data, false)) ||
			!__sync_bool_compare_and_swap(&this->m_data_index, this->m_max_data_index - resolved_rest_data, 0 )); 	//4. memory try-write
		
		//Weaker memory model needs explicit memory barrior instruction here.
        this->m_pos_consumer_index = tmp_pos_consumer_index;
        return resolved_block;
    }
}__attribute__((aligned(64)));

#endif
