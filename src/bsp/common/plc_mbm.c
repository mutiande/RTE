/*
 * FreeModbus Libary: user callback functions and buffer define in master mode
 * Copyright (C) 2013 Armink <armink.ztl@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: $Id: user_mbm_app_m.c, v 1.60 2013/11/23 11:49:05 Armink $
 */
#include <stdbool.h>
#include <stdint.h>

#include <plc_config.h>
#include <plc_abi.h>
#include <plc_diag.h>
#include <plc_rtc.h>
#include <plc_hw.h>
#include <plc_iom.h>

#include <mb.h>

//#include "user_mb_app.h"

#define MBM_REQUEST_MAX_ADDR 250
#define INVALID_UID 0xFFFF

enum req_type_enum
{
    RT_NONE,
    RT_DISCRETE,
    RT_INPUTREG,
    RT_HOLDING_RD,
    RT_HOLDING_WR,
    RT_COILS,
};

enum req_result_enum
{
    RR_SUCCESS=0,
    RR_TIMEOUT,
    RR_DATA_ERR,
    RR_INT_ERR,
    RR_LOADING,
    RR_PENDING,
    RR_NEXTSTEP,
    RR_FINISHED,
};

typedef enum req_type_enum req_type_t;
typedef enum req_result_enum req_result_t;

struct request_struct
{
    uint16_t uid;
    uint32_t period;
    req_type_t type;
    req_result_t result;
    uint8_t slave_addr;
    uint16_t target_addr[MBM_REQUEST_MAX_ADDR];
    uint16_t target_value[MBM_REQUEST_MAX_ADDR];
    uint8_t registers;
};

typedef struct request_struct request_t;

static request_t mbm_request;

//static uint8_t mbm_preread_buffer[(MBM_REQUEST_MAX_ADDR>>3)+1];
static uint16_t mbm_preread_buffer[MBM_REQUEST_MAX_ADDR];


static uint32_t mbm_nearest_time=0xFFFFFFFF;
static uint16_t mbm_nearest_num;

static uint8_t mbm_busy = 0xFF;
static bool mbm_need_preread = false;
static bool mbm_preread_finished = false;

static mb_inst_struct MBMaster;
static mb_trans_union MBMTransport;

static bool mbm_init_flg = false;
static bool mbm_start = false;

static bool mbm_gotinit = false;
static bool mbm_enabled = false;
static bool mbm_ascii = false;
uint32_t mbm_baudrate = 9600;


#define LOCAL_PROTO plc_mbm
void PLC_IOM_LOCAL_INIT(void)
{
    int i;

    mbm_request.type=RT_NONE;
    mbm_request.registers=0;
    mbm_request.result=RR_FINISHED;
    mbm_request.slave_addr=0;
    mbm_request.uid=INVALID_UID;
    for (i=0; i<MBM_REQUEST_MAX_ADDR; i++)
    {
        mbm_request.target_addr[i]=0;
        mbm_request.target_value[i]=0;
    }

}
bool PLC_IOM_LOCAL_TEST_HW(void)
{
    return true;
}
/*
static const char plc_mbm_err_asz[]     = "Modbus local register adress must be one number!";
static const char plc_mbm_err_tp[]      = "Modbus local supports only memory locations for registers!";
static const char plc_mbm_err_addr[]    = "Modbus local register adress must be in 0...31!";
static const char plc_mbm_err_init[]    = "Modbus wrong init value format. Must be QX2.[baudrate].[mode]";
static const char plc_mbm_err_tp_init[] = "Modbus local supports only output location for initialization!";

static const char plc_mbm_err_reglimit[]  = "Registers number limit reached for one modbus master request_t";
static const char plc_mbm_err_badtype[]  = "Modbus register type does not match request_t type";
static const char plc_mbm_err_requid[]  = "Modbus request_t UID out of range";
static const char plc_mbm_err_extrainit[] = "Only one modbus init variable allowed!";
*/

bool PLC_IOM_LOCAL_CHECK(uint16_t i)
{
//    int j;
//    uint32_t addr;
//    uint32_t temp;
//    req_type_t rtype;

    switch (PLC_APP->l_tab[i]->a_size)
    {
    case 2:
        if (PLC_APP->l_tab[i]->v_type == PLC_LT_Q) //init location
        {
            if (PLC_APP->l_tab[i]->v_size != PLC_LSZ_X)
            {
                //plc_curr_app->log_msg_post(LOG_CRITICAL, (char *)plc_mbm_err_tp_init, sizeof(plc_mbm_err_tp_init));
                plc_iom_errno_print(PLC_ERRNO_MBM_TP_INIT);
                return false;
            }
            if (mbm_gotinit)
            {
                //plc_curr_app->log_msg_post(LOG_CRITICAL, (char *)plc_mbm_err_extrainit, sizeof(plc_mbm_err_extrainit));
                plc_iom_errno_print(PLC_ERRNO_MBM_EX_INIT);
                return false;
            }

            mbm_gotinit = true;
            mbm_baudrate = (int)PLC_APP->l_tab[i]->a_data[0];
            mbm_ascii = ((int)PLC_APP->l_tab[i]->a_data[1]!=0);
            return true;
        }
        else if (PLC_APP->l_tab[i]->v_type == PLC_LT_M)//requested registers
        {
            if ((PLC_APP->l_tab[i]->v_size != PLC_LSZ_X) && (PLC_APP->l_tab[i]->v_size != PLC_LSZ_W)) //bool size only for coils and discrete inputs
            {
                //word size for holding & input regs
                //plc_curr_app->log_msg_post(LOG_CRITICAL, (char *)plc_mbm_err_badtype, sizeof(plc_mbm_err_badtype));
                plc_iom_errno_print(PLC_ERRNO_MBM_SZ);
                return false;
            }

        }
        else
        {
            //plc_curr_app->log_msg_post(LOG_CRITICAL, (char *)plc_mbm_err_tp, sizeof(plc_mbm_err_tp));
            plc_iom_errno_print(PLC_ERRNO_MBM_TP);
            return false;
        }
        break;

    case 4: //Request location
        if (PLC_APP->l_tab[i]->v_type == PLC_LT_I)
        {
            if (PLC_APP->l_tab[i]->v_size != PLC_LSZ_B)
            {
                //plc_curr_app->log_msg_post(LOG_CRITICAL, (char *)plc_mbm_err_badtype, sizeof(plc_mbm_err_badtype));
                plc_iom_errno_print(PLC_ERRNO_MBM_TP);
                return false;
            }
        }
        break;
    default:
        plc_iom_errno_print(PLC_ERRNO_MBM_ASZ);
        return false;
        break;
    }
    return true;
}

void PLC_IOM_LOCAL_START(uint16_t i)
{
    (void)i;
}

void PLC_IOM_LOCAL_STOP(void)
{
}

void PLC_IOM_LOCAL_BEGIN(uint16_t i)
{
    (void)i;
}

bool is_continious()
{
    if (!mbm_request.registers)
    {
        return false;
    }
    if ((mbm_request.target_addr[mbm_request.registers-1]-mbm_request.target_addr[0])==(mbm_request.registers-1))
    {
        return true;
    }
    return false;
}

uint8_t get_full_length()
{
    if (!mbm_request.registers)
    {
        return 0;
    }
    return mbm_request.target_addr[mbm_request.registers-1] - mbm_request.target_addr[0]+1;
}


//Do once!
void PLC_IOM_LOCAL_END(uint16_t i)
{
//    int j, k, l;
    (void)i;
    if (!mbm_init_flg)
    {
        mbm_init_flg = true;
        //If no init location specified then start with default params.
        if (!mbm_gotinit)
        {
            mbm_ascii = MB_DEFAULT_TRANSPORT;
            mbm_baudrate = MB_DEFAULT_BAUDRATE;
            mbm_enabled = true;
            mbm_start = true;
        }
        //mb_mstr_init_rtu(&MBMaster, &MBMTransport, (mb_port_base_struct *)&mbm_inst_usart, mbm_baudrate, MB_PAR_NONE);
        mb_init(&MBMaster, &MBMTransport, (mbm_ascii)?MB_ASCII:MB_RTU, TRUE, 0, (mb_port_base_struct *)&mbm_inst_usart, mbm_baudrate, MB_PAR_NONE);

    }
}

uint32_t PLC_IOM_LOCAL_SCHED(uint16_t lid, uint32_t tick)
{
    (void)lid;
    (void)tick;
    return 0;
}

void execute_request(uint32_t tick)
{
    int i;
    int j=0;
    uint8_t reg_index;

    mbm_busy=0x00;
    mbm_need_preread     = false;
    PLC_APP->w_tab[mbm_nearest_num] = tick;

    switch (mbm_request.type)
    {
    case RT_HOLDING_RD:
        mb_mstr_rq_read_holding_reg(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], get_full_length());
        break;
    case RT_HOLDING_WR:
        if (is_continious())
            mb_mstr_rq_write_multi_holding_reg(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], mbm_request.registers, mbm_request.target_value);
        else
        {
            if (mbm_preread_finished) //after we got current register values, change what we have to write and send a request_t to send everything to slave
            {
                mbm_preread_finished = false;
                for (j=0; j<get_full_length(); j++)
                {
                    reg_index = mbm_request.target_addr[j]-mbm_request.target_addr[0];
                    mbm_preread_buffer[reg_index] = mbm_request.target_value[j];
                }
                //mb_mstr_rq_write_holding_reg
                mb_mstr_rq_write_multi_holding_reg(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], get_full_length(), mbm_preread_buffer);
            }
            else //we need to send a request_t to first read full registers range
            {
                mb_mstr_rq_read_holding_reg(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], get_full_length());
                mbm_need_preread = true;
            }
        }
        break;
    case RT_DISCRETE:
        mb_mstr_rq_read_discrete_inputs(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], get_full_length());
        break;
    case RT_INPUTREG:
        mb_mstr_rq_read_inp_reg(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], get_full_length());
        break;
    case RT_COILS:
        //pack bits
        if(is_continious())
        {
            for (i=0; i<get_full_length(); i++)
            {
                mb_util_set_bits((UCHAR *)mbm_preread_buffer, i, 1, (mbm_request.target_value[i]==0)?0:1);
            }
            //send request_t
            mb_mstr_rq_write_multi_coils(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], get_full_length(), (UCHAR *)mbm_preread_buffer);
        }
        else
        {
            if (mbm_preread_finished) //after we got current register values, change what we have to write and send a request_t to send everything to slave
            {
                mbm_preread_finished = false;
                for (j=0; j<get_full_length(); j++)
                {
                    reg_index = mbm_request.target_addr[j]-mbm_request.target_addr[0];
                    mb_util_set_bits((UCHAR *)mbm_preread_buffer, reg_index, 1, (mbm_request.target_value[j]==0)?0:1);
                }
                //mb_mstr_rq_write_holding_reg
                mb_mstr_rq_write_multi_coils(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], get_full_length(), (UCHAR *)mbm_preread_buffer);
            }
            else //we need to send a request_t to first read full registers range
            {
                mb_mstr_rq_read_coils(&MBMaster, mbm_request.slave_addr, mbm_request.target_addr[0], get_full_length());
                mbm_need_preread = true;
            }
        }

        break;
    default:
        break;
    }
}

void read_request_info(uint16_t i)
{
    mbm_request.uid = PLC_APP->l_tab[i]->a_data[0];
    mbm_request.slave_addr = PLC_APP->l_tab[i]->a_data[2];
    mbm_request.period = PLC_APP->l_tab[i]->a_data[3];
    mbm_request.result = RR_LOADING;
    mbm_request.registers=0;
    switch (PLC_APP->l_tab[i]->a_data[1])
    {
    case 0:
        mbm_request.type = RT_INPUTREG;
        break;
    case 2:
        mbm_request.type = RT_HOLDING_WR;
        break;
    case 3:
        mbm_request.type = RT_COILS;
        break;
    case 4:
        mbm_request.type = RT_DISCRETE;
        break;

    case 1:
    default:
        mbm_request.type = RT_HOLDING_RD;
        break;
    }
}

bool request_completed(void)
{
    return ((mbm_request.result!=RR_LOADING) && (mbm_request.result!= RR_PENDING));
}

void PLC_IOM_LOCAL_POLL(uint32_t tick)
{
//    int i, j;
//    uint8_t max_weight_num;
//    uint8_t max_weight=0;

    if (!mbm_init_flg) return;

    //Do once!
    if (mbm_start)
    {
        mbm_start = false;
        if (!mbm_enabled)
        {
            mb_disable(&MBMaster);
            return;
        }
        mb_enable(&MBMaster);
    }

    if (mb_poll(&MBMaster)==MB_EILLSTATE)
    {
        return;
    }

    if (mbm_preread_finished)
    {
        execute_request(tick);
        return;
    }

    if (mbm_request.result==RR_NEXTSTEP)
    {
        mbm_request.result = RR_PENDING;
    }

    if (mbm_request.result==RR_LOADING)
    {
        if (mbm_request.registers)
        {
            if ((mbm_request.type==RT_COILS) || (mbm_request.type==RT_HOLDING_WR)) //write request_t, set values to write first
            {
                mbm_request.result=RR_NEXTSTEP;
            }
            else
            {
                mbm_request.result=RR_PENDING;
            }
        }
        else
        {
            //Skip empty request.
            mbm_request.result=RR_INT_ERR;
            PLC_APP->w_tab[mbm_nearest_num] = tick;
        }
    }


    if (mbm_busy!=0xFF)
    {
        return;
    }

    if (mbm_request.result==RR_PENDING)
    {
        if ((PLC_APP->w_tab[mbm_nearest_num]+mbm_request.period)<tick)
        {
            execute_request(tick);
        }
    }

    //check if it is time to execute request_t


}
uint32_t PLC_IOM_LOCAL_WEIGTH(uint16_t i)
{
    return PLC_APP->l_tab[i]->a_data[0];
}

uint8_t req_find_reg(uint16_t reg_addr)
{
    int i;
    for (i=0; i<mbm_request.registers; i++)
    {
        if (mbm_request.target_addr[i]==reg_addr)
        {
            return i;
        }
        if (mbm_request.target_addr[i]>reg_addr)
        {
            return 0xFF;
        }

    }
    return 0xFF;
}



uint32_t PLC_IOM_LOCAL_GET(uint16_t i)
{
    uint32_t last_called;
    switch (plc_curr_app->l_tab[i]->v_type)
    {
    case PLC_LT_I:
        if (mbm_request.result==RR_FINISHED)
        {
            last_called = (PLC_APP->l_tab[i]->a_data[3]+PLC_APP->w_tab[i]);
            if (last_called<mbm_nearest_time)
            {
                mbm_nearest_time = last_called;
                mbm_nearest_num = i;
            }
        }
        else if (mbm_request.uid==plc_curr_app->l_tab[i]->a_data[0])
        {
            if (request_completed())
            {
                *(IEC_USINT *)(plc_curr_app->l_tab[i]->v_buf) = mbm_request.result;
            }

        }
        break;
    case PLC_LT_M:
        mbm_nearest_time=0xFFFFFFFF;
        if (mbm_request.result==RR_FINISHED)
        {
            if (mbm_request.uid!=plc_curr_app->l_tab[mbm_nearest_num]->a_data[0])
            {
                read_request_info(mbm_nearest_num);
            }
            else
                mbm_request.result=RR_LOADING;

        }

        if (mbm_request.uid==plc_curr_app->l_tab[i]->a_data[0])
        {

            if (mbm_request.result==RR_LOADING)
            {
                mbm_request.target_addr[mbm_request.registers] = PLC_APP->l_tab[i]->a_data[1];
                mbm_request.registers++;
            }
            else if (request_completed())
            {
                switch (PLC_APP->l_tab[i]->v_size)
                {
                case PLC_LSZ_W:
                    *(IEC_UINT *)(plc_curr_app->l_tab[i]->v_buf) = mbm_request.target_value[req_find_reg(plc_curr_app->l_tab[i]->a_data[1])];
                    break;
                case PLC_LSZ_X:
                    *(IEC_BOOL *)(plc_curr_app->l_tab[i]->v_buf) = mbm_request.target_value[req_find_reg(plc_curr_app->l_tab[i]->a_data[1])];
                    break;
                }

            }

        }

        break;
    case PLC_LT_Q://Write only access to outputs
    default:
        break;
    }
    return 0;
}

uint32_t PLC_IOM_LOCAL_SET(uint16_t i)
{
    if (request_completed())
    {
        mbm_request.result = RR_FINISHED;
    }

    switch (plc_curr_app->l_tab[i]->v_type)
    {
    case PLC_LT_M:
        if (mbm_request.uid==plc_curr_app->l_tab[i]->a_data[0])
        {
            switch (PLC_APP->l_tab[i]->v_size)
            {
            case PLC_LSZ_W:
                mbm_request.target_value[req_find_reg(plc_curr_app->l_tab[i]->a_data[1])] = *(IEC_UINT *)(plc_curr_app->l_tab[i]->v_buf);
                break;
            case PLC_LSZ_X:
                mbm_request.target_value[req_find_reg(plc_curr_app->l_tab[i]->a_data[1])] = *(IEC_BOOL *)(plc_curr_app->l_tab[i]->v_buf);
                break;
            }
        }
        break;
    case PLC_LT_Q: //for init location.
        if (mbm_gotinit)
        {
            mbm_enabled = *(IEC_BOOL *)(PLC_APP->l_tab[i]->v_buf);
            mbm_start = true; //Now we can start
            mbm_gotinit = false; //Do not check locationy any more
        }
        break;

    case PLC_LT_I:
    default:
        break;
    }
    return 0;
}

/**
 * This is modbus master respond timeout error process callback function.
 * @note There functions will block modbus master poll while execute OS waiting.
 * So, for real-time of system.Do not execute too much waiting process.
 *
 * @param dst_addr destination salve address
 * @param pdu_data_ptr PDU buffer data
 * @param pdu_len PDU buffer length
 *
 */
void mb_mstr_error_timeout_cb(mb_inst_struct *inst, UCHAR dst_addr, const UCHAR* pdu_data_ptr, USHORT pdu_len)
{
    (void)inst;
    (void)dst_addr;
    (void)pdu_data_ptr;
    (void)pdu_len;
    mbm_request.result=RR_TIMEOUT;
    mbm_busy=0xFF;
}


/**
 * This is modbus master receive data error process callback function.
 * @note There functions will block modbus master poll while execute OS waiting.
 * So, for real-time of system.Do not execute too much waiting process.
 *
 * @param dst_addr destination salve address
 * @param pdu_data_ptr PDU buffer data
 * @param pdu_len PDU buffer length
 *
 */
void mb_mstr_error_rcv_data_cb(mb_inst_struct *inst, UCHAR dst_addr, const UCHAR* pdu_data_ptr, USHORT pdu_len)
{
    (void)inst;
    (void)dst_addr;
    (void)pdu_data_ptr;
    (void)pdu_len;
    mbm_request.result=RR_DATA_ERR;
    mbm_busy=0xFF;
}

/**
 * This is modbus master execute function error process callback function.
 * @note There functions will block modbus master poll while execute OS waiting.
 * So, for real-time of system.Do not execute too much waiting process.
 *
 * @param dst_addr destination salve address
 * @param pdu_data_ptr PDU buffer data
 * @param pdu_len PDU buffer length
 *
 */
void mb_mstr_error_exec_fn_cb(mb_inst_struct *inst, UCHAR dst_addr, const UCHAR* pdu_data_ptr, USHORT pdu_len)
{
    (void)inst;
    (void)dst_addr;
    (void)pdu_data_ptr;
    (void)pdu_len;
    mbm_request.result=RR_INT_ERR;
    mbm_busy=0xFF;
}

/**
 * This is modbus master request_t process success callback function.
 * @note There functions will block modbus master poll while execute OS waiting.
 * So, for real-time of system.Do not execute too much waiting process.
 *
 */
void mb_mstr_rq_success_cb(mb_inst_struct *inst)
{
    (void)inst;

    if (mbm_need_preread)
    {
        mbm_need_preread     = false;
        mbm_preread_finished = true;
    }
    else
    {
        mbm_request.result=RR_SUCCESS;
        mbm_busy=0xFF;
    }

}

/**
 * Modbus master input register callback function.
 *
 * @param reg_buff input register buffer
 * @param reg_addr input register address
 * @param reg_num input register number
 *
 * @return result
 */
mb_err_enum mb_mstr_reg_input_cb(mb_inst_struct *inst, UCHAR *reg_buff, USHORT reg_addr, USHORT reg_num)
{
    mb_err_enum    status = MB_ENOERR;
    uint8_t reg_index;

    (void)inst;

    /* it already plus one in modbus function method. */
    reg_addr--;

    while (reg_num > 0)
    {
        reg_index = req_find_reg(reg_addr);

        mbm_request.target_value[reg_index] =  (uint16_t)(*reg_buff++)<<8;
        mbm_request.target_value[reg_index]|=   *reg_buff++;

        reg_addr++;
        reg_num--;
    }

    return status;
}

/**
 * Modbus master holding register callback function.
 *
 * @param reg_buff holding register buffer
 * @param reg_addr holding register address
 * @param reg_num holding register number
 * @param mode read or write
 *
 * @return result
 */



mb_err_enum mb_mstr_reg_holding_cb(mb_inst_struct *inst, UCHAR *reg_buff, USHORT reg_addr, USHORT reg_num)
{
    mb_err_enum    status = MB_ENOERR;

    uint8_t reg_index=0;

    (void)inst;

    reg_addr--;

    while (reg_num > 0)
    {
        if (mbm_need_preread)
        {
            mbm_preread_buffer[reg_index] = (uint16_t)(*reg_buff++)<<8;
            mbm_preread_buffer[reg_index]|=   *reg_buff++;
            reg_index++;
        }
        else
        {
            reg_index = req_find_reg(reg_addr);
            if (reg_index!=0xFF)
            {
                mbm_request.target_value[reg_index] =  (uint16_t)(*reg_buff++)<<8;
                mbm_request.target_value[reg_index]|=   *reg_buff++;
            }
            else
            {
                reg_buff++;
                reg_buff++;
                //we got extra register. just ignore it for now
            }
        }
        reg_addr++;
        reg_num--;
    }

//    switch (mode)
//    {
//    /* write values to slave registers*/
//    case MB_REG_WRITE:
//        break;
//        while (reg_num > 0)
//        {
//            reg_index = req_find_reg(reg_addr);
//            if (reg_index!=0xFF)
//            {
//                *reg_buff++ = (UCHAR) (mbm_request.target_value[reg_index] >> 8);
//                *reg_buff++ = (UCHAR) (mbm_request.target_value[reg_index] & 0xFF);
//            }
//            else //means we are trying to write registers we do not have
//            {
//                *reg_buff++=0xDE; //should never end up here
//                *reg_buff++=0xAD; //
//            }
//            reg_addr++;
//            reg_num--;
//        }
//        break;
//    /*Get values from slave registers */
//    case MB_REG_READ:
//        while (reg_num > 0)
//        {
//            if (mbm_need_preread)
//            {
//                mbm_preread_buffer[reg_index] = (uint16_t)(*reg_buff++)<<8;
//                mbm_preread_buffer[reg_index]|=   *reg_buff++;
//                reg_index++;
//            }
//            else
//            {
//                reg_index = req_find_reg(reg_addr);
//                if (reg_index!=0xFF)
//                {
//                    mbm_request.target_value[reg_index] =  (uint16_t)(*reg_buff++)<<8;
//                    mbm_request.target_value[reg_index]|=   *reg_buff++;
//                }
//                else
//                {
//                    reg_buff++;
//                    reg_buff++;
//                    //we got extra register. just ignore it for now
//                }
//            }
//            reg_addr++;
//            reg_num--;
//        }
//        break;
//    }

    return status;
}

/**
 * Modbus master coils callback function.
 *
 * @param reg_buff coils buffer
 * @param reg_addr coils address
 * @param coil_num coils number
 * @param mode read or write
 *
 * @return result
 */
mb_err_enum mb_mstr_reg_coils_cb(mb_inst_struct *inst, UCHAR *reg_buff, USHORT reg_addr, USHORT coil_num)
{
    mb_err_enum    status = MB_ENOERR;
    uint8_t nByte=0;

    (void)inst;
    (void)reg_addr;

    if (mbm_need_preread)
    {
        nByte = coil_num/8 + ((coil_num%8)>0)?1:0;
        memcpy(mbm_preread_buffer, reg_buff, nByte);
// Эта реализация memcpy не работает!!!
//        while(nByte>0)
//        {
//            mbm_preread_buffer[nByte] = *(reg_buff+nByte);
//            nByte--;
//        }
// Эта реализация memcpy не работает!!!
    }
    else
    {
        //coils read callback here
    }

    return status;
}

/**
 * Modbus master discrete callback function.
 *
 * @param reg_buff discrete buffer
 * @param reg_addr discrete address
 * @param disc_num discrete number
 *
 * @return result
 */
mb_err_enum mb_mstr_reg_discrete_cb(mb_inst_struct *inst, UCHAR *reg_buff, USHORT reg_addr, USHORT disc_num)
{
    mb_err_enum    status = MB_ENOERR;

    uint8_t reg_num =  0;
    uint8_t reg_index;

    (void)inst;

    /* it already plus one in modbus function method. */
    reg_addr--;

    /* write current discrete values with new values from the protocol stack. */
    while (disc_num > 0)
    {
        reg_index = req_find_reg(reg_addr+reg_num);
        if (reg_index!=0xFF)
        {
            mbm_request.target_value[reg_index] = (*(reg_buff+(reg_num/8))) &(1<<(reg_num%8));
        }

        if ((reg_num%8)==7)
        {
            reg_buff++;
        }
        disc_num--;
        reg_num++;
    }

    return status;
}

