# DIGITAL ESP32 RETRO CAMERA
I want to have a camera for myself to taking pictures and videos around and not having rely to much on my phone. So I try to create one using esp32-s wroom camera AIthinker.

# Minimum requirements:
## Version 1.0 (in progress)
* Can take picture and record video (save to SD-card).
* Can review taken images/ video (from SD-card).
* Having an LCD to see what's being capture right now (from camera).
* Having total 4 buttons with features:
    * 1 to start/stop capture image or record video.
    * 1 to change camera capture mode (capture image <-> record video)
    * 1 to enter/exit gallery.
    * 2 to scroll to previous/next items in gallery.
    * 1 button can share many features, as long as intuitive and convienence. (machine state for buttons will be added later).


## Camera
* camera_task (liên tục ktra events queue, nếu có task của camera thì sẽ thực hiện)
* video_task (liên tục ktra events queue, nếu có task của video thì sẽ thực hiện)
* camera_init (khởi camera với các cấu hình đã chọn)
* camera_start (khởi tạo các task cho camera và video) // sau này cần gán task cho core cụ thể
* camera_driver_init (khởi tạo driver cho cảm biến camera / sử dụng trong camera_init)
* camera_flash_init (khởi tạo đèn flash của module camera)
* camera_set_capture_mode (cài đặt chế độ ghi hình hiện tại của camera)
* camera_get_capture_mode (lấy thông tin chế độ hiện tại của camera, để hiện thị và hệ thống nhận biết)
* camera_handle_toggle_capture_mode (thay đổi qua lại chế độ ghi hình của camera)
* camera_set_flash_mode (cài đặt chế độ của đèn flash)
* camera_get_flash_mode (lấy thông tin chế độ đèn flash)
* camera_handle_toggle_flash_mode (thay đổi chế độ của đèn flash)

* camera_handle_capture (camera xử lý khi có events chụp ảnh từ inputs)
* camera_handle_open_gallery (mở gallery để xem ảnh và video, khi có events từ inputs)* camera_save_photo (lưu ảnh chụp được vào thẻ nhớ, sử dụng trong camera_capture_photo)

* camera_capture_photo (camera_handle_capture, nếu đang ở mode chụp ảnh)
* camera_capture_video (camera_handle_capture, nếu đang ở mode quay video)
    * camera_is_recording (ktra camera có đang quay video hay ko)
    * camera_start_video (bắt đầu quay video)
    * camera_stop_video (dừng quay video)
    * camera_record_frame (xử lý ghép từng frame ảnh thành video)

## Events
* events_channel_valid (ktra channel khi đky có tồn tại ko)
* events_channel_to_mask (chuyển channel từ số nguyên sang mask bit nhị phân)
* events_lock (khóa events bằng cách lấy semaphore để tránh xung đột khi chạy task)
* events_unlock (mở khóa events bằng cách trả semaphore lại)
* events_find_free_slot (tìm và trả về con trỏ cho vị trí còn trống trong ... )
* events_validate_subscriber (ktra tính hợp lệ của subscriber / bên nhận và xử lý events)
* events_is_initialized (trả về trạng thái khởi tạo của events)
* events_init (khởi tạo events, ktra thạng thái events r tạo semaphore/mutex và subscribers của events)
* events_subscribe (đky channel và queue_size cho events)
* events_publish (đẩy events lên hàng đợi events)
* events_receive (ktra subscribers có nhận được events từ hàng đợi events chưa)
* events_unsubscribe (hủy đky nhận events)

## Mode
* mode_init (khởi tạo semaphore cho quản lý events và tạo mode mặc định của camera là chụp ảnh)
* mode_get (trả về trạng thái hiện tại của thiết bị)
* mode_set (cài đặt trạng thái cho thiết bị)

## Inputs
* input_debounce (debounce cho nút bấm của inputs)
* input_gpio_isr (thực hiện interrupts cho nút bấm inputs)
* input_translate (ánh xạ vị trí nút với chức năng dựa trên chế độ của thiết bị)
* input_task (tạo task đảm nhận xử lý inputs)
* input_init (khởi tạo queue cho inputs, cấu hình và thiết lập gpio cho các nút bấm)
* input_start (tạo task cho nút bấm để xử lý)

## Storage
* storage_scan_directory (ktra đường dẫn thông tin trạng thái dữ liệu trong thẻ nhớ)
* storage_load_index (tải dữ liệu trạng thái của dữ liệu trong thẻ)
* storage_save_index (lưu trạng thái dữ liệu trong thẻ / dùng khi lưu thêm dữ liệu mới vào thẻ)
* storage_init (cấu hình và kết nối thẻ nhớ vs camera)
* storage_save_jpeg (lưu ảnh từ frame buffer của camera vào thẻ nhớ)
* storage_latest_path (trả về đường dẫn gần đây của camera/ )
* storage_latest_filename (trả về tên file bức ảnh gần đây nhất)
* storage_image_count (trả về số bức ảnh trong thẻ nhớ)
* storage_deinit (ngắt kết nối thẻ nhớ với camera)
* sdcard_save_file (lưu file vào thẻ nhớ)
* uri_to_path (mapping file trong thẻ nhớ theo request api, photos hoặc frontend webserver)
* storage_open_uri (mở file frontend từ thẻ nhớ)
* storage_write_u32 (hỗ trợ lưu video)
* storage_write_fourcc (hỗ trợ lưu video)
* storage_video_writer_header (tạo header cho để lưu video)
* storage_video_reserve_index (ktra còn đủ dung lượng để cấp phát lưu video ko)
* storage_video_create (tạo file video với height, width, fps và tên tại header trước đó)
* storage_video_write_frame (đẩy từng frame vào file video để tạo thành video)
* storage_video_close (đóng file video để finalize và lưu thành 1 file video hoàn chỉnh vào thẻ nhớ)
* storage_video_abort (loại bỏ file video trước đó nếu đang quay video mà bị hủy)

## Display
* lcd_init
* lcd_deinit
* 