
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
                read_only image2d_t imgR,
                read_only image2d_t imgG,
                read_only image2d_t imgB,
                uint width,
                uint height,
               __global uchar* pDest )
{
    int tid_x = get_global_id( 0 );
    int tid_y = get_global_id( 1 );
    uint4 colorR, colorG, colorB;
    int2 coord;

    const sampler_t samplerA = CLK_NORMALIZED_COORDS_FALSE |
                               CLK_ADDRESS_NONE         |
                               CLK_FILTER_NEAREST;

    if (0 && tid_x == 0 && tid_y == 0)
    {
        int2 dim2 = get_image_dim(imgR);
        printf("**** w = %d, h = %d, dim = (%d, %d), channel_data_type = 0x%08x, channel_order = 0x%08x\n", 
            get_image_width(imgR), 
            get_image_height(imgR), 
            dim2[0], dim2[1], 
            get_image_channel_data_type(imgR), 
            get_image_channel_order(imgR));

        uint4 color;
        color = read_imageui( imgR, samplerA, (int2)(0, 0));
        printf("**** color = [%d, %d, %d, %d]\n", color.x, color.y, color.z, color.w);
    }
            
    if( tid_x < width && tid_y < height )
    {
        coord = ( int2 )( tid_x, tid_y );

        if( ( ( tid_y * width ) + tid_x ) < ( width * height ) )
        {
            colorR = read_imageui( imgR, samplerA, coord );
            pDest[ ( tid_y * width ) + tid_x ] = ( uchar ) colorR.x; 

            colorG = read_imageui( imgG, samplerA, coord );
            pDest[ ( tid_y * width ) + tid_x + width*height] = ( uchar ) colorG.x; 

            colorB = read_imageui( imgB, samplerA, coord );
            pDest[ ( tid_y * width ) + tid_x + width*height*2] = ( uchar ) colorB.x; 
        }
    }
}
