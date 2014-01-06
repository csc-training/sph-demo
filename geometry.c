#include <stdio.h>
#include "geometry.h"
#include "fluid.h"

void constructFluidVolume(fluid_particle **fluid_particle_pointers, fluid_particle *fluid_particles, AABB* fluid, int start_x, int number_particles_x, edge *edges, param *params)
{
    double spacing;
    int num_y;
    int num_z;
    
    spacing = params->spacing_particle;
    // Number of particles in y,z, number in x is passed in
    num_y = floor((fluid->max_y - fluid->min_y ) / spacing);
    num_z = floor((fluid->max_z - fluid->min_z ) / spacing);
    
    // zero out number of edge particles
    edges->number_edge_particles_left = 0;
    edges->number_edge_particles_right = 0;
    
    // Place particles inside bounding volume
    double x,y,z;
    int nx,ny,nz;
    int i = 0;
    fluid_particle *p;
    for(nz=0; nz<num_z; nz++) {
        z = fluid->min_z + nz*spacing;
        for(ny=0; ny<num_y; ny++) {
            y = fluid->min_y + ny*spacing;
            for(nx=0; nx<number_particles_x; nx++) {
                x = fluid->min_x + (start_x + nx)*spacing;
                p = fluid_particles + i;
                p->x = x;
                p->y = y;
                p->z = z;
                
                // Set pointer array
                fluid_particle_pointers[i] = p;
	        fluid_particle_pointers[i]->id = i;
                i++;
            }
        }
    }

    printf("rank %d max fluid x: %f\n", params->rank,fluid->min_x + (start_x + nx-1)*spacing);

    params->number_fluid_particles_local = i;
    params->max_fluid_particle_index = i - 1;
}

// Sets upper bound on number of particles, used for memory allocation
void setParticleNumbers(AABB *boundary_global, AABB *fluid_global, edge *edges, oob *out_of_bounds, int number_particles_x, param *params)
{
    int num_x;
    int num_y;
    int num_z;
    
    double spacing = params->spacing_particle;

    // Set fluid local
    num_x = number_particles_x;
    num_y = floor((fluid_global->max_y - fluid_global->min_y ) / spacing);
    num_z = floor((fluid_global->max_z - fluid_global->min_z ) / spacing);

    // Maximum edge particles is a set to 4 particle width y,z slab
    edges->max_edge_particles = 4 * num_y*num_z;
    out_of_bounds->max_oob_particles = 4 * num_y*num_z;

    // Initial fluid particles
    int num_initial = num_x * num_y * num_z;
    printf("initial number of particles %d\n", num_initial);
    int num_extra = num_initial/5;

    params->max_node_difference = num_extra/2;
 
    // Add initial space, extra space for particle transfers, and left/right out of boudns/halo particles
    params->max_fluid_particles_local = num_initial + num_extra + 2*out_of_bounds->max_oob_particles + 2*edges->max_edge_particles;

    out_of_bounds->number_vacancies = 0.0;
    
}

// Set local boundary and fluid particle
void partitionProblem(AABB *boundary_global, AABB *fluid_global, int *x_start, int *length_x, param *params)
{
    int i;
    int nprocs = params->nprocs;
    int rank = params->rank;
    double spacing = params->spacing_particle;
        
    // number of fluid particles in x direction
    // +1 added for zeroth particle
    int fluid_particles_x = floor((fluid_global->max_x - fluid_global->min_x ) / spacing) + 1;
    
    // number of particles x direction
    int *particle_length_x = malloc(nprocs*sizeof(int));
    
    // Number of particles in x direction assuming equal spacing
    int equal_spacing = floor(fluid_particles_x/nprocs);
    
    // Initialize each node to have equal width
    for (i=0; i<nprocs; i++)
        particle_length_x[i] = equal_spacing;
    
    // Remaining particles from equal division
    int remaining = fluid_particles_x - (equal_spacing * nprocs);
    
    // Add any remaining particles sequantially to left most nodes
    for (i=0; i<nprocs; i++)
        particle_length_x[i] += (i<remaining?1:0);
    
    // Number of particles to left of current node
    int number_to_left = 0;
    for (i=0; i<rank; i++)
        number_to_left+=particle_length_x[i];
       
    // starting position of nodes x particles
    *x_start = number_to_left;
    // Number of particles in x direction for node
    *length_x = particle_length_x[rank];
        
    // Set node partition values
    params->node_start_x = fluid_global->min_x + ((number_to_left-1) * spacing);
    params->node_end_x   = params->node_start_x + (particle_length_x[rank] * spacing);
    
    if (rank == 0)
        params->node_start_x  = boundary_global->min_x;
    if (rank == nprocs-1)
        params->node_end_x   = boundary_global->max_x;

    free(particle_length_x);    

    printf("rank %d, h %f, x_start %d, num_x %d, start_x %f, end_x: %f\n", rank, params->spacing_particle, *x_start, *length_x, params->node_start_x, params->node_end_x);
    
}

// Test if boundaries need to be adjusted
void checkPartition(fluid_particle **fluid_particle_pointers, oob *out_of_bounds, param *params)
{
    
    int i;
    fluid_particle *p;
    int max_diff = params->max_node_difference;
    int num_rank = params->number_fluid_particles_local;
    int rank = params->rank;
    int nprocs = params->nprocs;
    double h = params->spacing_particle;

    // Setup nodes to left and right of self
    int proc_to_left =  (rank == 0 ? MPI_PROC_NULL : rank-1);
    int proc_to_right = (rank == nprocs-1 ? MPI_PROC_NULL : rank+1);

    // Get number of particles and partition length  from right and left
    double length = params->node_end_x - params->node_start_x;
    double node[2] = {(double)num_rank, length};
    double left[2];
    double right[2];
    int tag = 627;
    // Send number of particles to  right and receive from left
    MPI_Sendrecv(node, 2, MPI_DOUBLE, proc_to_right, tag, left,2,MPI_DOUBLE,proc_to_left,tag,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    // Send number of particles to left and receive from right
    tag = 895;
    MPI_Sendrecv(node, 2, MPI_DOUBLE, proc_to_left, tag, right,2,MPI_DOUBLE,proc_to_right,tag,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

    // Number of particles in left/right ranks
    int num_left = (int)left[0];
    int num_right = (int)right[0];

    // Partition length of left/right ranks
    double length_left = left[1];
    double length_right = right[1];

    // Difference in particle numbers of left/right ranks
    int diff_left = num_rank-num_left;
    int diff_right = num_rank-num_right;

    // Adjust left boundary
    // Ensure partition length is atleast 4*h
    if (rank != 0) // Dont move left most boundary
    {
        if( diff_left > max_diff && length > 4*h) 
            params->node_start_x += h;
        else if (diff_left < -max_diff && length_left > 4*h)
            params->node_start_x -= h;
    }
    // Adjust right boundary
    if (rank != (nprocs-1))
    {
        if( diff_right > max_diff && length > 4*h)
            params->node_end_x -= h;
        else if (diff_right < -max_diff && length_right > 4*h)
            params->node_end_x += h;
    }

//    printf("rank %d node_start %f node_end %f \n", rank, params->node_start_x, params->node_end_x);

    // Reset out of bound numbers
    out_of_bounds->number_oob_particles_left = 0;
    out_of_bounds->number_oob_particles_right = 0;

    // Identifiy out of boundary particles
    for(i=0; i<params->number_fluid_particles_local; i++) {
        p = fluid_particle_pointers[i];

        // Set OOB particle indicies and update number
        {
        if (p->x < params->node_start_x)
            out_of_bounds->oob_pointer_indicies_left[out_of_bounds->number_oob_particles_left++] = i;
        else if (p->x > params->node_end_x)
            out_of_bounds->oob_pointer_indicies_right[out_of_bounds->number_oob_particles_right++] = i;
       }
    }

}

////////////////////////////////////////////////
// Utility Functions
////////////////////////////////////////////////
double min(double a, double b){
    double min = a;
    min = b < min ? b : min;
    return min;
}

double max(double a, double b){
    double max = a;
    max = b > max ? b : max;
    return max;
}

int sgn(double x) {
    int val = 0;
    if (x < 0.0)
        val = -1;
    else if (x > 0.0)
        val = 1;

    return val;
}