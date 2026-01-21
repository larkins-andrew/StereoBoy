#include "adc.h"

uint16_t capture_buf[CAPTURE_DEPTH];

int main() {
    stdio_init_all();
    sleep_ms(2000); // Wait for serial to settle
    printf("RP2350 Dual-Channel Continuous ADC Example\n");

    // --- 1. ADC Setup ---
    adc_init();

    // Initialize the GPIO pins for ADC functionality
    adc_gpio_init(ADC_PIN_0);
    adc_gpio_init(ADC_PIN_1);

    // Set the Round Robin mask
    // Bit 0 = Channel 0, Bit 1 = Channel 1.
    // Mask 0x03 (binary 0011) means "sequence Ch0 -> Ch1 -> Ch0..."
    adc_set_round_robin(0x03);

    // Select the first channel to start the sequence
    adc_select_input(0);

    // Configure the FIFO
    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ (and IRQ) asserted when at least 1 sample is present
        false,   // We won't check the error bit
        false    // Do not shift each sample to 8 bits (keep 12 bits)
    );

    // Set sample rate
    // 48MHz clock / (47999 + 1) = 1000 samples per second
    adc_set_clkdiv(47999); 

    // --- 2. DMA Setup ---
    int dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);

    // Reading from constant address (ADC FIFO), writing to incrementing address (buffer)
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    
    // Pace transfers based on ADC availability
    channel_config_set_dreq(&cfg, DREQ_ADC);

    // --- 3. Main Loop ---
    
    // Start the ADC in free-running mode ONCE.
    // It will fill the FIFO, and DMA will drain it as fast as it appears.
    adc_run(true);

    while (true) {
        // Configure and start DMA for a new batch
        dma_channel_configure(
            dma_chan,
            &cfg,
            capture_buf,    // Destination
            &adc_hw->fifo,  // Source
            CAPTURE_DEPTH,  // Number of transfers
            true            // Start immediately
        );

        // Wait for the buffer to fill
        // In a real application, you might use an interrupt (IRQ) here instead of blocking
        dma_channel_wait_for_finish_blocking(dma_chan);

        // --- Process Data ---
        // Data is interleaved: [Ch0, Ch1, Ch0, Ch1, ...]
        printf("--- Batch Complete ---\n");
        printf("Ch0: %d  |  Ch1: %d\n", capture_buf[0], capture_buf[1]);
        printf("Ch0: %d  |  Ch1: %d\n", capture_buf[2], capture_buf[3]);
        
        // The ADC is still running in the background!
        // We just loop back and restart the DMA to catch the next batch.
    }
}