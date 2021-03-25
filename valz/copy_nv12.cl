
// const sampler_t image_sampler =
//     CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;

__kernel void ReadNV12KernelFromNV12(
                read_only image2d_t nv12Img,
                uint width,
                uint height,
               __global uchar* pDest )
{
    int tid_x = get_global_id( 0 );
    int tid_y = get_global_id( 1 );
    float4 colorY;
    int2 coord;

    const sampler_t samplerA = CLK_NORMALIZED_COORDS_FALSE |
                               CLK_ADDRESS_NONE         |
                               CLK_FILTER_NEAREST;
                               
    if( tid_x < width && tid_y < height )
    {
        coord = ( int2 )( tid_x, tid_y );

        if( ( ( tid_y * width ) + tid_x ) < ( width * height ) )
        {
            colorY = read_imagef( nv12Img, samplerA, coord );
            pDest[ ( tid_y * width ) + tid_x ] = ( uchar ) ( 256.0 * colorY.y ); // (uchar)((tid_y * width) + tid_x);
            //printf("%f, ", colorY.y);
            
            if( ( tid_x % 2 == 0 ) && ( tid_y % 2 == 0 ) )
            {
                pDest[ (width * height) + ( tid_y / 2 * width ) + ( tid_x ) ]       = ( uchar )( 256.0 * colorY.z );
                pDest[ (width * height) + ( tid_y / 2 * width ) + ( tid_x ) + 1 ]   = ( uchar )( 256.0 * colorY.x );
            }
        }
    }
}
