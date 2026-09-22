// --------------------------------------------------------------------- //
// You will need to modify this file.                                    //
// You may add any code you need, as long as you correctly implement the //
// required pipe_cycle_*() functions already listed in this file.        //
// In part B, you will also need to implement pipe_check_bpred().        //
// --------------------------------------------------------------------- //

// pipeline.cpp
// Implements functions to simulate a pipelined processor.

#include "pipeline.h"
#include <cstdlib>
#include <stdio.h>
#include <unistd.h>

/**
 * Read a single trace record from the trace file and use it to populate the
 * given fetch_op.
 * 
 * You should not modify this function.
 * 
 * @param p the pipeline whose trace file should be read
 * @param fetch_op the PipelineLatch struct to populate
 */
void pipe_get_fetch_op(Pipeline *p, PipelineLatch *fetch_op)
{
    TraceRec *trace_rec = &fetch_op->trace_rec;
    uint8_t *trace_rec_buf = (uint8_t *)trace_rec;
    size_t bytes_read_total = 0;
    ssize_t bytes_read_last = 0;
    size_t bytes_left = sizeof(*trace_rec);

    // Read a total of sizeof(TraceRec) bytes from the trace file.
    while (bytes_left > 0)
    {
        bytes_read_last = read(p->trace_fd, trace_rec_buf, bytes_left);
        if (bytes_read_last <= 0)
        {
            // EOF or error
            break;
        }

        trace_rec_buf += bytes_read_last;
        bytes_read_total += bytes_read_last;
        bytes_left -= bytes_read_last;
    }

    // Check for error conditions.
    if (bytes_left > 0 || trace_rec->op_type >= NUM_OP_TYPES)
    {
        fetch_op->valid = false;
        p->halt_op_id = p->last_op_id;

        if (p->last_op_id == 0)
        {
            p->halt = true;
        }

        if (bytes_read_last == -1)
        {
            fprintf(stderr, "\n");
            perror("Couldn't read from pipe");
            return;
        }

        if (bytes_read_total == 0)
        {
            // No more trace records to read
            return;
        }

        // Too few bytes read or invalid op_type
        fprintf(stderr, "\n");
        fprintf(stderr, "Error: Invalid trace file\n");
        return;
    }

    // Got a valid trace record!
    fetch_op->valid = true;
    fetch_op->stall = false;
    fetch_op->is_mispred_cbr = false;
    fetch_op->op_id = ++p->last_op_id;
}

/**
 * Allocate and initialize a new pipeline.
 * 
 * You should not need to modify this function.
 * 
 * @param trace_fd the file descriptor from which to read trace records
 * @return a pointer to a newly allocated pipeline
 */
Pipeline *pipe_init(int trace_fd)
{
    printf("\n** PIPELINE IS %d WIDE **\n\n", PIPE_WIDTH);

    // Allocate pipeline.
    Pipeline *p = (Pipeline *)calloc(1, sizeof(Pipeline));

    // Initialize pipeline.
    p->trace_fd = trace_fd;
    p->halt_op_id = (uint64_t)(-1) - 3;

    // Allocate and initialize a branch predictor if needed.
    if (BPRED_POLICY != BPRED_PERFECT)
    {
        p->b_pred = new BPred(BPRED_POLICY);
    }

    return p;
}

/**
 * Print out the state of the pipeline latches for debugging purposes.
 * 
 * You may use this function to help debug your pipeline implementation, but
 * please remove calls to this function before submitting the lab.
 * 
 * @param p the pipeline
 */
void pipe_print_state(Pipeline *p)
{
    printf("\n--------------------------------------------\n");
    printf("Cycle count: %lu, retired instructions: %lu\n",
           (unsigned long)p->stat_num_cycle,
           (unsigned long)p->stat_retired_inst);

    // Print table header
    for (uint8_t latch_type = 0; latch_type < NUM_LATCH_TYPES; latch_type++)
    {
        switch (latch_type)
        {
        case IF_LATCH:
            printf(" IF:    ");
            break;
        case ID_LATCH:
            printf(" ID:    ");
            break;
        case EX_LATCH:
            printf(" EX:    ");
            break;
        case MA_LATCH:
            printf(" MA:    ");
            break;
        default:
            printf(" ------ ");
        }
    }
    printf("\n");

    // Print row for each lane in pipeline width
    for (uint8_t i = 0; i < PIPE_WIDTH; i++)
    {
        for (uint8_t latch_type = 0; latch_type < NUM_LATCH_TYPES;
             latch_type++)
        {
            if (p->pipe_latch[latch_type][i].valid)
            {
                printf(" %6lu ",
                       (unsigned long)p->pipe_latch[latch_type][i].op_id);
            }
            else
            {
                printf(" ------ ");
            }
        }
        printf("\n");
    }
    printf("\n");
}

/**
 * Simulate one cycle of all stages of a pipeline.
 * 
 * You should not need to modify this function except for debugging purposes.
 * If you add code to print debug output in this function, remove it or comment
 * it out before you submit the lab.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle(Pipeline *p)
{
    p->stat_num_cycle++;

    // In hardware, all pipeline stages execute in parallel, and each pipeline
    // latch is populated at the start of the next clock cycle.

    // In our simulator, we simulate the pipeline stages one at a time in
    // reverse order, from the Write Back stage (WB) to the Fetch stage (IF).
    // We do this so that each stage can read from the latch before it and
    // write to the latch after it without needing to "double-buffer" the
    // latches.

    // Additionally, it means that earlier pipeline stages can know about
    // stalls triggered in later pipeline stages in the same cycle, as would be
    // the case with hardware stall signals asserted by combinational logic.
//    pipe_print_state(p);
    pipe_cycle_WB(p);
    pipe_cycle_MA(p);
    pipe_cycle_EX(p);
    pipe_cycle_ID(p);
    pipe_cycle_IF(p);

    // You can uncomment the following line to print out the pipeline state
    // after each clock cycle for debugging purposes.
    // Make sure you comment it out or remove it before you submit the lab.
    //pipe_print_state(p);
}

/**
 * Simulate one cycle of the Write Back stage (WB) of a pipeline.
 * 
 * Some skeleton code has been provided for you. You must implement anything
 * else you need for the pipeline simulation to work properly.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_WB(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (p->pipe_latch[MA_LATCH][i].valid)
        {
            p->stat_retired_inst++;

            if (p->pipe_latch[MA_LATCH][i].op_id >= p->halt_op_id)
            {
                // Halt the pipeline if we've reached the end of the trace.
                p->halt = true;
            }
        }
    }
}

/**
 * Simulate one cycle of the Memory Access stage (MA) of a pipeline.
 * 
 * Some skeleton code has been provided for you. You must implement anything
 * else you need for the pipeline simulation to work properly.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_MA(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        // Copy each instruction from the EX latch to the MA latch.
        p->pipe_latch[MA_LATCH][i] = p->pipe_latch[EX_LATCH][i];
    }
}

/**
 * Simulate one cycle of the Execute stage (EX) of a pipeline.
 * 
 * Some skeleton code has been provided for you. You must implement anything
 * else you need for the pipeline simulation to work properly.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_EX(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        // Copy each instruction from the ID latch to the EX latch.
	if(p->pipe_latch[ID_LATCH][i].valid && p->pipe_latch[ID_LATCH][i].stall)
	{
       		 p->pipe_latch[EX_LATCH][i].valid = false;
       	}
       	else
       	{
       		p->pipe_latch[EX_LATCH][i] = p->pipe_latch[ID_LATCH][i];
       	}
    }
}

/**
 * Simulate one cycle of the Instruction Decode stage (ID) of a pipeline.
 * 
 * Some skeleton code has been provided for you. You must implement anything
 * else you need for the pipeline simulation to work properly.
 * 
 * @param p the pipeline to simulate
 */

//bool hazard(const auto& dataID, const auto& data_EX, const auto& data_MA,const auto& PIPE_WIDTH)


void pipe_cycle_ID(Pipeline *p)
{
    uint64_t min_op_id=UINT64_MAX;
	bool stall = false;
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        // Copy each instruction from the IF latch to the ID latch.
  //      p->pipe_latch[ID_LATCH][i] = p->pipe_latch[IF_LATCH][i];
	if(!(p->pipe_latch[ID_LATCH][i].valid && p->pipe_latch[ID_LATCH][i].stall))
	{
		p->pipe_latch[ID_LATCH][i] = p->pipe_latch[IF_LATCH][i];
		p->pipe_latch[IF_LATCH][i].valid=false;
	}

	TraceRec &data_ID = p->pipe_latch[ID_LATCH][i].trace_rec;
	bool ID_valid = p->pipe_latch[ID_LATCH][i].valid;
	uint64_t op_id_i=p->pipe_latch[ID_LATCH][i].op_id;
	uint64_t youngest_op_id_src1=0;
	uint64_t youngest_op_id_src2=0;
	int EX_hazard_src1=0;
	int MA_hazard_src1=0;
	int ID_hazard_src1=0;
	int load_flag_src1=0;
	//unsigned int lane_EX_src1=9;
	// unsigned int lane_MA_src1=9;
	// unsigned int lane_ID_src1=9;
	unsigned int EX_hazard_src2=0;
	int MA_hazard_src2=0;
	int ID_hazard_src2=0;
	int load_flag_src2=0;
	// unsigned int lane_EX_src2=9;
	// unsigned int lane_MA_src2=9;
	// unsigned int lane_ID_src2=9;
	// unsigned int lane_ID_cc=9;
	// unsigned int lane_EX_cc=9;
	// unsigned int lane_MA_cc=9;
	uint64_t youngest_op_id_cc=0;
	int EX_hazard_cc=0;
	int MA_hazard_cc=0;
	int ID_hazard_cc=0;
	int load_flag_cc=0;
	int src1_stall=0;
	int src2_stall=0;
	int cc_stall=0;
		
	if(ID_valid)
	{
	for(unsigned int k=0; k<PIPE_WIDTH; k++)
	{
		if(k==i)
		{continue;}
		TraceRec &data_ID_lane = p->pipe_latch[ID_LATCH][k].trace_rec;
		uint64_t op_id_ID = p->pipe_latch[ID_LATCH][k].op_id;
		bool ID_lane_valid = p->pipe_latch[ID_LATCH][k].valid;
		if(data_ID_lane.dest_needed==true && data_ID_lane.dest_reg==data_ID.src1_reg && data_ID.src1_needed == true && ID_lane_valid && op_id_i>op_id_ID)
		{
			if(youngest_op_id_src1 <= op_id_ID)
			{
				youngest_op_id_src1=op_id_ID;
				ID_hazard_src1=1;
				EX_hazard_src1=0;
				MA_hazard_src1=0;
				//lane_ID_src1 = k;
			}
		}

		if(data_ID_lane.dest_needed==true && data_ID_lane.dest_reg==data_ID.src2_reg && data_ID.src2_needed == true && ID_lane_valid && op_id_i>op_id_ID)
		{
			if(youngest_op_id_src2 <= op_id_ID)
			{
				youngest_op_id_src2=op_id_ID;
				ID_hazard_src2=1;
				EX_hazard_src2=0;
				MA_hazard_src2=0;
				//lane_ID_src2 = k;
			}
		}
		if(ID_lane_valid && data_ID_lane.cc_write && data_ID.cc_read && op_id_i>op_id_ID)
		{
			if(youngest_op_id_cc <= op_id_ID)
			{
				youngest_op_id_cc=op_id_ID;
				ID_hazard_cc=1;
				EX_hazard_cc=0;
				MA_hazard_cc=0;
				//lane_ID_cc = k;
			}
		}
	}

	for(unsigned int j=0; j<PIPE_WIDTH; j++)
	{
//		uint64_t youngest_op_id_EX=MAX_INT;
//		uint64_t youngest_op_id_MA=MAX_INT;
		TraceRec &data_EX = p->pipe_latch[EX_LATCH][j].trace_rec;
		TraceRec &data_MA = p->pipe_latch[MA_LATCH][j].trace_rec;
		uint64_t op_id_EX = p->pipe_latch[EX_LATCH][j].op_id;
		uint64_t op_id_MA = p->pipe_latch[MA_LATCH][j].op_id;
//		int MA_hazard=0;
//		int EX_hazard=0;
//		int load_flag=0;
		bool EX_valid = p->pipe_latch[EX_LATCH][j].valid;
		bool MA_valid = p->pipe_latch[MA_LATCH][j].valid;
		if(EX_valid && data_EX.dest_needed==true && data_EX.dest_reg==data_ID.src1_reg && data_ID.src1_needed==true  && op_id_i>op_id_EX)
		{
			if(youngest_op_id_src1<= op_id_EX)
			{
				youngest_op_id_src1=op_id_EX;
				EX_hazard_src1=1;
				ID_hazard_src1=0;
				MA_hazard_src1=0;
				if(data_EX.op_type==OP_LD)
				{
					load_flag_src1=1;
				}
				else
				{
					load_flag_src1=0;
				}
				//lane_EX_src1=j;

			}
		}

		if(EX_valid && data_EX.dest_needed==true && data_EX.dest_reg==data_ID.src2_reg && data_ID.src2_needed==true && op_id_i>op_id_EX)
		{
			if(youngest_op_id_src2<= op_id_EX)
			{
				youngest_op_id_src2=op_id_EX;
				EX_hazard_src2=1;
				ID_hazard_src2=0;
				MA_hazard_src2=0;
				if(data_EX.op_type==OP_LD)
				{
					load_flag_src2=1;
				}
				else
				{
					load_flag_src2=0;
				}
				//lane_EX_src2=j;
			}
		}


		if(EX_valid && data_EX.cc_write && data_ID.cc_read && op_id_i>op_id_EX)
		{
			if(youngest_op_id_cc<= op_id_EX)
			{
				youngest_op_id_cc=op_id_EX;
				EX_hazard_cc=1;
				ID_hazard_cc=0;
				MA_hazard_cc=0;
				if(data_EX.op_type==OP_LD)
				{
					load_flag_cc=1;
				}
				else
				{
					load_flag_cc=0;
				}
				//lane_EX_cc=j;
			}
		}



		if(MA_valid && data_MA.dest_needed==true && data_MA.dest_reg==data_ID.src1_reg && data_ID.src1_needed==true && op_id_i>op_id_MA )
		{
			if(youngest_op_id_src1<=op_id_MA)
			{
				youngest_op_id_src1=op_id_MA;
				MA_hazard_src1=1;
				EX_hazard_src1=0;
				ID_hazard_src1=0;
				//lane_MA_src1=j;
			}
		}

		if(MA_valid && data_MA.dest_needed==true &&data_MA.dest_reg==data_ID.src2_reg && data_ID.src2_needed==true && op_id_i > op_id_MA )
		{
			if(youngest_op_id_src2<=op_id_MA)
			{
				youngest_op_id_src2=op_id_MA;
				MA_hazard_src2=1;
				EX_hazard_src2=0;
				ID_hazard_src2=0;
				//lane_MA_src2=j;
			}
		}
		if(MA_valid && data_MA.cc_write && data_ID.cc_read && op_id_i > op_id_MA)
		{
			if(youngest_op_id_cc<=op_id_MA)
			{
				youngest_op_id_cc=op_id_MA;
				MA_hazard_cc=1;
				EX_hazard_cc=0;
				ID_hazard_cc=0;
				//lane_MA_cc=j;
			}
		}
	}

	if(ID_hazard_src1)
	{
		//stall all lanes younger and equal to lane_ID_src1
		src1_stall=1;
		if(min_op_id>op_id_i){min_op_id=op_id_i;}
	//	p->pipe_latch[ID_LATCH][lane_ID_src1].stall = true;
	}
	else if(EX_hazard_src1)
	{
		if(ENABLE_EXE_FWD)
		{
			if(load_flag_src1==1)
			{
				//stall 1 cycle
				src1_stall=1;
				if(min_op_id>op_id_i){min_op_id=op_id_i;}
			//	p->pipe_latch[ID_LATCH][lane_EX_src1].stall = true;
			}
			else
			{
				//forward
				src1_stall=0;
			}
		}
		else
		{
			//stall 1 cycle
			src1_stall=1;
			if(min_op_id>op_id_i){min_op_id=op_id_i;}
		//	p->pipe_latch[ID_LATCH][lane_EX_src1].stall = true;
		}
	}

	else if(MA_hazard_src1)
	{
		if(ENABLE_MEM_FWD)
		{
			//forward
			src1_stall=0;
		}
		else
		{
			//stall 1 cycle
			src1_stall=1;
			if(min_op_id>op_id_i){min_op_id=op_id_i;}
		//	p->pipe_latch[ID_LATCH][lane_MA_src1].stall = true;
		}
	}

	else
	{
		//continue as normal
		src1_stall=0;
	}

	if(ID_hazard_src2)
	{
		//stall all lanes younger and equal to lane_ID_src1
	//	p->pipe_latch[ID_LATCH][].stall = true;
		src2_stall=1;
		if(min_op_id>op_id_i){min_op_id=op_id_i;}
	}
	else if(EX_hazard_src2)
	{
		if(ENABLE_EXE_FWD)
		{
			if(load_flag_src2==1)
			{
				//stall 1 cycle
				src2_stall=1;
				if(min_op_id>op_id_i){min_op_id=op_id_i;}
			//	p->pipe_latch[ID_LATCH][lane_EX_src2].stall = true;
			}
			else
			{
				//forward
				src2_stall=0;
			}
		}
		else
		{
			//stall 1 cycle
			src2_stall=1;
			if(min_op_id>op_id_i){min_op_id=op_id_i;}
		//	p->pipe_latch[ID_LATCH][lane_EX_src2].stall = true;
		}
	}

	else if(MA_hazard_src2)
	{
		if(ENABLE_MEM_FWD)
		{
			//forward
			src2_stall=0;
		}
		else
		{
			//stall 1 cycle
			src2_stall=1;
		//	if(min_lane>i){min_lane=i;}
			if(min_op_id>op_id_i){min_op_id=op_id_i;}
		//	p->pipe_latch[ID_LATCH][lane_MA_src2].stall = true;
		}
	}

	else
	{
		//continue as normal
		src2_stall=0;
	}

	if(ID_hazard_cc)
	{
		//stall all lanes younger and equal to lane_ID_src1
	//	p->pipe_latch[ID_LATCH][lane_ID_cc].stall = true;
//		if(min_lane>i){min_lane=i;}
		if(min_op_id>op_id_i){min_op_id=op_id_i;}
		cc_stall=1;
	}
	else if(EX_hazard_cc)
	{
		if(ENABLE_EXE_FWD)
		{
			if(load_flag_cc==1)
			{
				//stall 1 cycle
				cc_stall=1;
//				if(min_lane>i){min_lane=i;}
				if(min_op_id>op_id_i){min_op_id=op_id_i;}
			//	p->pipe_latch[ID_LATCH][lane_EX_cc].stall = true;
			}
			else
			{
				//forward
				cc_stall=0;
			}
		}
		else
		{
			//stall 1 cycle
			cc_stall=1;
//			if(min_lane>i){min_lane=i;}
			if(min_op_id>op_id_i){min_op_id=op_id_i;}
		//	p->pipe_latch[ID_LATCH][lane_EX_cc].stall = true;
		}
	}

	else if(MA_hazard_cc)
	{
		if(ENABLE_MEM_FWD)
		{
			//forward
			cc_stall=0;
		}
		else
		{
			//stall 1 cycle
			cc_stall=1;
//			if(min_lane>i){min_lane=i;}
			if(min_op_id>op_id_i){min_op_id=op_id_i;}
		//	p->pipe_latch[ID_LATCH][lane_MA_cc].stall = true;
		}
	}

	else
	{
		//continue as normal
		cc_stall=0;
	}
	if((src1_stall==1 || src2_stall==1 || cc_stall==1) && stall==false)
	{stall=true;}
    }
    else
    {
    //not valid instruction nop
    	p->pipe_latch[ID_LATCH][i] = p->pipe_latch[IF_LATCH][i];
    }
  
}

if(stall==true)
{
for(unsigned int i=0; i<PIPE_WIDTH;i++)
{
	uint64_t op_id_ID = p->pipe_latch[ID_LATCH][i].op_id;
	if(op_id_ID<min_op_id)
	{
		p->pipe_latch[ID_LATCH][i].stall=false;
	}
	else
	{
		p->pipe_latch[ID_LATCH][i].stall=true;
	}
}
}
else{
	for(unsigned int i=0;i<PIPE_WIDTH;i++)
	{
		p->pipe_latch[ID_LATCH][i].stall=false;
	}
}
}

/**
 * Simulate one cycle of the Instruction Fetch stage (IF) of a pipeline.
 * 
 * Some skeleton code has been provided for you. You must implement anything
 * else you need for the pipeline simulation to work properly.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_IF(Pipeline *p)
{
    for(unsigned int i =0; i<PIPE_WIDTH; i++)
    {
    	if(p->pipe_latch[IF_LATCH][i].valid || p->pipe_latch[ID_LATCH][i].stall)
    	{
    		continue;
    	}
        // Read an instruction from the trace file.
//	if(p->last_op_id >= p->halt_op_id)
//	{
//		continue;
//	}
        PipelineLatch fetch_op;
        pipe_get_fetch_op(p, &fetch_op);

        // Handle branch (mis)prediction.
        if (BPRED_POLICY != BPRED_PERFECT)
        {
            pipe_check_bpred(p, &fetch_op);
        }

        // Copy the instruction to the IF latch.
        p->pipe_latch[IF_LATCH][i] = fetch_op;
    }
}

/**
 * If the instruction just fetched is a conditional branch, check for a branch
 * misprediction, update the branch predictor, and set appropriate flags in the
 * pipeline.
 * 
 * You must implement this function in part B of the lab.
 * 
 * @param p the pipeline
 * @param fetch_op the pipeline latch containing the operation fetched
 */
void pipe_check_bpred(Pipeline *p, PipelineLatch *fetch_op)
{
    // TODO: For a conditional branch instruction, get a prediction from the
    // branch predictor.

    // TODO: If the branch predictor mispredicted, mark the fetch_op
    // accordingly.

    // TODO: Immediately update the branch predictor.

    // TODO: If needed, stall the IF stage by setting the flag
    // p->fetch_cbr_stall.
}
