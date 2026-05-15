#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "esp_log.h"
#include "esp_libusb.h"

static class_adsb_dev *adsbdev;

void init_adsb_dev()
{
    /* Idempotent across reconnects: rtlsdr_open calls this on every
     * attach. Without the free below, every hot-plug cycle leaked a
     * class_adsb_dev struct (~32 B) plus its pending transfer (~6 KB),
     * and — worse — any in-flight URB completion callback would write
     * its result fields into freed memory once we overwrote adsbdev. */
    if (adsbdev) {
        if (adsbdev->transfer) {
            usb_host_transfer_free(adsbdev->transfer);
            adsbdev->transfer = NULL;
        }
        free(adsbdev->response_buf);
        free(adsbdev);
    }
    adsbdev = calloc(1, sizeof(class_adsb_dev));
    adsbdev->is_adsb = true;
}
void bulk_transfer_read_cb(usb_transfer_t *transfer)
{
    // int in_xfer = transfer->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK;
    // if ((transfer->status == 0) && in_xfer)
    // {
    //     for (int i = 0; i < 10; i++)
    //         fprintf(stdout, "%02X", transfer->data_buffer[i]);
    //     fprintf(stdout, "\n");
    //     // for (int i = 0; i < transfer->actual_num_bytes; i++)
    //     // {
    //     //     adsbdev->response_buf[i] = transfer->data_buffer[i];
    //     // }
    // }

    adsbdev->is_done = true;
    adsbdev->is_success = transfer->status == 0;
    adsbdev->bytes_transferred = transfer->actual_num_bytes;
    //  printf("BULK: Transfer:Read type %d\n", transfer->actual_num_bytes);
    // printf("BULK: Transfer:Read status %d, actual number of bytes transferred %d, databuffer size %d, %d\n", transfer->status, transfer->actual_num_bytes, transfer->data_buffer_size, adsbdev->response_buf[8]);
}

void transfer_read_cb(usb_transfer_t *transfer)
{
    for (int i = 0; i < transfer->actual_num_bytes; i++)
    {
        adsbdev->response_buf[i] = transfer->data_buffer[i];
    }
    adsbdev->is_done = true;
    adsbdev->is_success = transfer->status == 0;
    adsbdev->bytes_transferred = transfer->actual_num_bytes - sizeof(usb_setup_packet_t);
    // printf("Transfer:Read type %d %ld \n", transfer->actual_num_bytes, transfer->flags);
    // printf("Transfer:Read status %d, actual number of bytes transferred %d, databuffer size %d, %d\n", transfer->status, transfer->actual_num_bytes, transfer->data_buffer[8], adsbdev->response_buf[8]);
}

int esp_libusb_bulk_transfer(class_driver_t *driver_obj, unsigned char endpoint, unsigned char *data, int length, int *transferred, unsigned int timeout)
{
    ESP_ERROR_CHECK(usb_host_transfer_free(adsbdev->transfer));
    adsbdev->response_buf = NULL;
    // free(adsbdev->response_buf);
    size_t sizePacket = usb_round_up_to_mps(length, 512);
    esp_err_t r = usb_host_transfer_alloc(sizePacket, 0, &adsbdev->transfer);
    if (r != ESP_OK)
    {
        ESP_LOGI(TAG_ADSB, "esp_libusb_bulk_transfer failed with %d", r);
        return -1;
    }

    // ESP_LOGI(TAG_ADSB, "esp_libusb_bulk_transfer usb_host_transfer_alloc %d %d", sizePacket, length);
    adsbdev->transfer->num_bytes = sizePacket;
    adsbdev->transfer->device_handle = driver_obj->dev_hdl;
    adsbdev->transfer->timeout_ms = timeout;
    adsbdev->transfer->bEndpointAddress = endpoint;
    adsbdev->transfer->callback = bulk_transfer_read_cb;
    adsbdev->transfer->context = (void *)&driver_obj;
    adsbdev->is_done = false;
    r = usb_host_transfer_submit(adsbdev->transfer);
    if (r != ESP_OK)
    {
        ESP_LOGI(TAG_ADSB, "esp_libusb_bulk_transfer failed with %d", r);
        return -1;
    }
    while (!adsbdev->is_done)
    {
        usb_host_client_handle_events(driver_obj->client_hdl, portMAX_DELAY);
    }
    if (!adsbdev->is_success)
    {
        ESP_LOGI(TAG_ADSB, "esp_libusb_bulk_transfer failed");
        return -1;
    }
    *transferred = adsbdev->bytes_transferred;
    memcpy(data, adsbdev->transfer->data_buffer, *transferred);
    return 0;
}

int esp_libusb_control_transfer(class_driver_t *driver_obj, uint8_t bm_req_type, uint8_t b_request, uint16_t wValue, uint16_t wIndex, unsigned char *data, uint16_t wLength, unsigned int timeout)
{
    /* Up to 3 attempts total: ESP-IDF's USB host stack will log every
     * STALL response on EP 0 to the console (`USBH: Dev N EP 0 STALL`),
     * but on a freshly re-opened RTL2832U the first one or two vendor
     * writes routinely STALL while the chip's USB front-end finishes
     * settling. Without an outer retry here, those failures silently
     * propagate up through rtlsdr_write_reg into rtlsdr_init_baseband
     * — which ignores every per-write return value — leaving the
     * demod half-configured and ADS-B decoding broken until the next
     * reboot. A small inter-attempt delay gives the dongle time to
     * land in its default state. */
    const int   max_attempts = 3;
    const int   inter_attempt_delay_ms = 5;
    size_t      sizePacket = sizeof(usb_setup_packet_t) + wLength;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        ESP_ERROR_CHECK(usb_host_transfer_free(adsbdev->transfer));
        free(adsbdev->response_buf);
        adsbdev->response_buf = NULL;
        adsbdev->transfer = NULL;

        usb_host_transfer_alloc(sizePacket, 0, &adsbdev->transfer);
        USB_SETUP_PACKET_INIT_CONTROL((usb_setup_packet_t *)adsbdev->transfer->data_buffer, bm_req_type, b_request, wValue, wIndex, wLength);
        adsbdev->transfer->num_bytes      = sizePacket;
        adsbdev->transfer->device_handle  = driver_obj->dev_hdl;
        adsbdev->transfer->timeout_ms     = timeout;
        adsbdev->transfer->context        = (void *)&driver_obj;
        adsbdev->transfer->callback       = transfer_read_cb;
        adsbdev->is_done                  = false;
        adsbdev->response_buf             = calloc(sizePacket, sizeof(uint8_t));

        if (bm_req_type == CTRL_OUT) {
            for (uint8_t i = 0; i < wLength; i++) {
                adsbdev->transfer->data_buffer[sizeof(usb_setup_packet_t) + i] = data[i];
            }
        }

        esp_err_t r = usb_host_transfer_submit_control(driver_obj->client_hdl, adsbdev->transfer);
        if (r != ESP_OK) {
            if (attempt + 1 < max_attempts) {
                vTaskDelay(pdMS_TO_TICKS(inter_attempt_delay_ms));
                continue;
            }
            ESP_LOGI(TAG_ADSB, "libusb_control_transfer failed with %d after %d attempts", r, attempt + 1);
            return -1;
        }

        while (!adsbdev->is_done) {
            usb_host_client_handle_events(driver_obj->client_hdl, portMAX_DELAY);
        }

        if (!adsbdev->is_success) {
            if (attempt + 1 < max_attempts) {
                vTaskDelay(pdMS_TO_TICKS(inter_attempt_delay_ms));
                continue;
            }
            ESP_LOGI(TAG_ADSB, "libusb_control_transfer failed after %d attempts", attempt + 1);
            return -1;
        }

        /* Success — copy IN data back and return. */
        for (uint8_t i = 0; i < wLength; i++) {
            data[i] = adsbdev->response_buf[sizeof(usb_setup_packet_t) + i];
        }
        return adsbdev->bytes_transferred;
    }
    return -1;  /* unreachable */
}

void esp_libusb_get_string_descriptor_ascii(const usb_str_desc_t *str_desc, char *str)
{
    if (str_desc == NULL)
    {
        return;
    }

    for (int i = 0; i < str_desc->bLength / 2; i++)
    {
        str[i] = (char)str_desc->wData[i];
    }
}
