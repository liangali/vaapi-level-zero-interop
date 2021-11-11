
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
            pDest[ ( tid_y * width ) + tid_x ] = ( uchar ) ( 255.0 * colorY.y ); // (uchar)((tid_y * width) + tid_x);//printf("%f, ", colorY.y);
            //printf("%f, ", colorY.y);
            
            if( ( tid_x % 2 == 0 ) && ( tid_y % 2 == 0 ) )
            {
                pDest[ (width * height) + ( tid_y / 2 * width ) + ( tid_x ) ]       = ( uchar )( 256.0 * colorY.z );
                pDest[ (width * height) + ( tid_y / 2 * width ) + ( tid_x ) + 1 ]   = ( uchar )( 256.0 * colorY.x );
            }
        }
    }
}

__kernel void ReadRGBPImage(
                read_only image2d_t nv12Img,
                uint width,
                uint height,
               __global uchar* pDest )
{
    int tid_x = get_global_id( 0 );
    int tid_y = get_global_id( 1 );
    uint4 colorY;
    int2 coord;

    const sampler_t samplerA = CLK_NORMALIZED_COORDS_FALSE |
                               CLK_ADDRESS_NONE         |
                               CLK_FILTER_NEAREST;

    if (tid_x == 0 && tid_y == 0)
    {
        int2 dim2 = get_image_dim(nv12Img);
        printf("**** w = %d, h = %d, dim = (%d, %d), channel_data_type = 0x%08x, channel_order = 0x%08x\n", 
            get_image_width(nv12Img), 
            get_image_height(nv12Img), 
            dim2[0], dim2[1], 
            get_image_channel_data_type(nv12Img), 
            get_image_channel_order(nv12Img));

        uint4 colorR;
        colorR = read_imageui( nv12Img, samplerA, (int2)(0, 0));
        printf("**** colorR = [%d, %d, %d, %d]\n", colorR.x, colorR.y, colorR.z, colorR.w);

    }


                               
    if( tid_x < width && tid_y < height )
    {
        coord = ( int2 )( tid_x, tid_y );

        if( ( ( tid_y * width ) + tid_x ) < ( width * height ) )
        {
            colorY = read_imageui( nv12Img, samplerA, coord );
            pDest[ ( tid_y * width ) + tid_x ] = ( uchar ) colorY.x;
            // printf("%f, ", colorY.x);
            
            // if( ( tid_x % 2 == 0 ) && ( tid_y % 2 == 0 ) )
            // {
            //     pDest[ (width * height) + ( tid_y / 2 * width ) + ( tid_x ) ]       = ( uchar )( 256.0 * colorY.z );
            //     pDest[ (width * height) + ( tid_y / 2 * width ) + ( tid_x ) + 1 ]   = ( uchar )( 256.0 * colorY.x );
            // }
        }
    }
}
