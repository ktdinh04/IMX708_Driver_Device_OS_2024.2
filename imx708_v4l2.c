#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/gpio/consumer.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>

static int qbc_adjust = 2;
module_param(qbc_adjust, int, 0644);
MODULE_PARM_DESC(qbc_adjust, "Quad Bayer broken line correction strength [0,2-5]");

#define IMX708_REG_VALUE_08BIT		1
#define IMX708_REG_VALUE_16BIT		2

/* Chip ID */
#define IMX708_REG_CHIP_ID		0x0016
#define IMX708_CHIP_ID			0x0708

#define IMX708_REG_MODE_SELECT		0x0100
#define IMX708_MODE_STANDBY		0x00
#define IMX708_MODE_STREAMING		0x01

#define IMX708_REG_ORIENTATION		0x101

#define IMX708_INCLK_FREQ		24000000

/* Default initial pixel rate, will get updated for each mode. */
#define IMX708_INITIAL_PIXEL_RATE	590000000

/* V_TIMING internal */
#define IMX708_REG_FRAME_LENGTH		0x0340
#define IMX708_FRAME_LENGTH_MAX		0xffff

/* Long exposure multiplier */
#define IMX708_LONG_EXP_SHIFT_MAX	7
#define IMX708_LONG_EXP_SHIFT_REG	0x3100

/* Exposure control */
#define IMX708_REG_EXPOSURE		0x0202
#define IMX708_EXPOSURE_OFFSET		48
#define IMX708_EXPOSURE_DEFAULT		0x640
#define IMX708_EXPOSURE_STEP		1
#define IMX708_EXPOSURE_MIN		1
#define IMX708_EXPOSURE_MAX		(IMX708_FRAME_LENGTH_MAX - \
					 IMX708_EXPOSURE_OFFSET)

/* Analog gain control */
#define IMX708_REG_ANALOG_GAIN		0x0204
#define IMX708_ANA_GAIN_MIN		112
#define IMX708_ANA_GAIN_MAX		960
#define IMX708_ANA_GAIN_STEP		1
#define IMX708_ANA_GAIN_DEFAULT	   IMX708_ANA_GAIN_MIN

/* Digital gain control */
#define IMX708_REG_DIGITAL_GAIN		0x020e
#define IMX708_DGTL_GAIN_MIN		0x0100
#define IMX708_DGTL_GAIN_MAX		0xffff
#define IMX708_DGTL_GAIN_DEFAULT	0x0100
#define IMX708_DGTL_GAIN_STEP		1

/* Colour balance controls */
#define IMX708_REG_COLOUR_BALANCE_RED   0x0b90
#define IMX708_REG_COLOUR_BALANCE_BLUE	0x0b92
#define IMX708_COLOUR_BALANCE_MIN	0x01
#define IMX708_COLOUR_BALANCE_MAX	0xffff
#define IMX708_COLOUR_BALANCE_STEP	0x01
#define IMX708_COLOUR_BALANCE_DEFAULT	0x100

/* Test Pattern Control */
#define IMX708_REG_TEST_PATTERN		0x0600
#define IMX708_TEST_PATTERN_DISABLE	0
#define IMX708_TEST_PATTERN_SOLID_COLOR	1
#define IMX708_TEST_PATTERN_COLOR_BARS	2
#define IMX708_TEST_PATTERN_GREY_COLOR	3
#define IMX708_TEST_PATTERN_PN9		4

/* Test pattern colour components */
#define IMX708_REG_TEST_PATTERN_R	0x0602
#define IMX708_REG_TEST_PATTERN_GR	0x0604
#define IMX708_REG_TEST_PATTERN_B	0x0606
#define IMX708_REG_TEST_PATTERN_GB	0x0608
#define IMX708_TEST_PATTERN_COLOUR_MIN	0
#define IMX708_TEST_PATTERN_COLOUR_MAX	0x0fff
#define IMX708_TEST_PATTERN_COLOUR_STEP	1

#define IMX708_REG_BASE_SPC_GAINS_L	0x7b10
#define IMX708_REG_BASE_SPC_GAINS_R	0x7c00

/* HDR exposure ratio (long:med == med:short) */
#define IMX708_HDR_EXPOSURE_RATIO       4
#define IMX708_REG_MID_EXPOSURE		0x3116
#define IMX708_REG_SHT_EXPOSURE		0x0224
#define IMX708_REG_MID_ANALOG_GAIN	0x3118
#define IMX708_REG_SHT_ANALOG_GAIN	0x0216

/* QBC Re-mosaic broken line correction registers */
#define IMX708_LPF_INTENSITY_EN		0xC428
#define IMX708_LPF_INTENSITY_ENABLED	0x00
#define IMX708_LPF_INTENSITY_DISABLED	0x01
#define IMX708_LPF_INTENSITY		0xC429

// Cấu trúc riêng cho driver cảm biến IMX708
struct imx708_device {
    struct v4l2_subdev sd; // Đối tượng V4L2 subdev
    struct media_pad pad; // Pad cho media controller
    struct i2c_client *client; // Đối tượng I2C client
    struct clk *xclk; // Clock đầu vào (XCLK)
    struct gpio_desc *reset_gpio; // Chân Reset (nếu có)
    struct regulator *vana1; // Nguồn cấp VANA1 (ví dụ)
    struct regulator *vdana; // Nguồn cấp VDDL (ví dụ)
    struct regulator *vdig; // Nguồn cấp VDIG (ví dụ)

};

// Hàm hỗ trợ đọc/ghi thanh ghi I2C (ví dụ đơn giản cho 16-bit address, 8-bit value)
// Driver thực tế có thể sử dụng i2c_regmap hoặc các hàm phức tạp hơn
static int imx708_read_reg(struct imx708_device *imx708, u16 reg, u8 *val)
{
    int ret;
    struct i2c_msg msgs[2];
    u8 tx_data[2];

    tx_data[0] = (reg >> 8) & 0xFF; // MSB
    tx_data[1] = reg & 0xFF;       // LSB

    msgs[0].addr = imx708->client->addr;
    msgs[0].flags = 0; // Write
    msgs[0].len = 2;
    msgs[0].buf = tx_data;

    msgs[1].addr = imx708->client->addr;
    msgs[1].flags = I2C_M_RD; // Read
    msgs[1].len = 1;
    msgs[1].buf = val;

    ret = i2c_transfer(imx708->client->adapter, msgs, 2);
    if (ret != 2) {
        dev_err(&imx708->client->dev, "i2c read reg 0x%04x failed, ret=%d\n", reg, ret);
        return -EIO;
    }

    return 0;
}

static int imx708_write_reg(struct imx708_device *imx708, u16 reg, u8 val)
{
    int ret;
    struct i2c_msg msgs[1];
    u8 tx_data[3];

    tx_data[0] = (reg >> 8) & 0xFF; // MSB
    tx_data[1] = reg & 0xFF;       // LSB
    tx_data[2] = val;

    msgs[0].addr = imx708->client->addr;
    msgs[0].flags = 0; // Write
    msgs[0].len = 3;
    msgs[0].buf = tx_data;

    ret = i2c_transfer(imx708->client->adapter, msgs, 1);
    if (ret != 1) {
        dev_err(&imx708->client->dev, "i2c write reg 0x%04x failed, ret=%d\n", reg, ret);
        return -EIO;
    }

    return 0;
}

// Hàm cấp nguồn cho cảm biến (cần lấy từ driver gốc, bao gồm trình tự bật các nguồn VANA, VDIG, VDDL và clock)
static int imx708_power_on(struct device *dev)
{
    struct imx708_device *imx708 = dev_get_drvdata(dev);
    int ret;

    dev_info(dev, "Powering on IMX708\n");

    // Bật các nguồn cấp (cần cấp nguồn theo đúng thứ tự và thời gian trễ yêu cầu của datasheet)
    // ret = regulator_enable(imx708->vana1); ...
    // ret = regulator_enable(imx708->vddl); ...
    // ret = regulator_enable(imx708->vdig); ...

    // Bật XCLK
    // ret = clk_prepare_enable(imx708->xclk); ...

    // Reset cảm biến bằng GPIO (nếu có chân reset)
    // gpiod_set_value_cansleep(imx708->reset_gpio, 1); // Kích hoạt reset
    // usleep_range(1000, 2000); // Thời gian trễ giữ reset
    // gpiod_set_value_cansleep(imx708->reset_gpio, 0); // Nhả reset
    // usleep_range(5000, 10000); // Thời gian trễ sau reset

    dev_info(dev, "IMX708 powered on\n");
    return 0; // Trả về 0 nếu thành công
}

// Hàm tắt nguồn cho cảm biến
static int imx708_power_off(struct device *dev)
{
    struct imx708_device *imx708 = dev_get_drvdata(dev);

    dev_info(dev, "Powering off IMX708\n");

    // Tắt XCLK
    // clk_disable_unprepare(imx708->xclk);

    // Tắt các nguồn cấp (cần tắt theo đúng thứ tự ngược lại với khi bật)
    // regulator_disable(imx708->vdig);
    // regulator_disable(imx708->vddl);
    // regulator_disable(imx708->vana1);

    dev_info(dev, "IMX708 powered off\n");
    return 0;
}


// Hàm kiểm tra Chip ID để xác định cảm biến có đúng là IMX708 không
static int imx708_check_id(struct imx708_device *imx708)
{
    u8 chip_id_val;
    int ret;

    // Cần đảm bảo cảm biến đã được cấp nguồn trước khi đọc ID
    // imx708_power_on(&imx708->client->dev); // Hoặc hàm power on được gọi trước đó trong probe

    ret = imx708_read_reg(imx708, IMX708_REG_CHIP_ID, &chip_id_val);
    if (ret) {
        dev_err(&imx708->client->dev, "Failed to read chip ID register\n");
        // imx708_power_off(&imx708->client->dev); // Tắt nguồn nếu bật trong hàm này
        return ret;
    }

    dev_info(&imx708->client->dev, "Read chip ID value: 0x%02x\n", chip_id_val);

    // So sánh với giá trị Chip ID mong đợi (cần xác nhận giá trị đúng từ driver gốc)
    if (chip_id_val == IMX708_CHIP_ID_VALUE) {
        dev_info(&imx708->client->dev, "Sony IMX708 sensor detected\n");
        ret = 0;
    } else {
        dev_err(&imx708->client->dev, "Invalid chip ID 0x%02x, expected 0x%02x\n",
                chip_id_val, IMX708_CHIP_ID_VALUE);
        ret = -ENODEV; // No such device
    }

    // imx708_power_off(&imx708->client->dev); // Tắt nguồn nếu bật trong hàm này

    return ret;
}

// Hàm cấu hình cảm biến ở chế độ mặc định (cần lấy sequence ghi thanh ghi từ driver gốc)
static int imx708_set_default_mode(struct imx708_device *imx708)
{
    dev_info(&imx708->client->dev, "Setting default mode\n");
    // Ghi các thanh ghi để cấu hình độ phân giải, tốc độ khung hình, v.v.
    // imx708_write_reg(imx708, REG_SOME_CONFIG, VALUE);
    // imx708_write_reg(imx708, REG_ANOTHER_CONFIG, VALUE);
    // ... rất nhiều thanh ghi cần cấu hình ...
    return 0;
}

// --- Các hàm V4L2 subdev operations ---

// Core operations (ví dụ: s_power)
static int imx708_s_power(struct v4l2_subdev *sd, int enable)
{
    struct imx708_device *imx708 = container_of(sd, struct imx708_device, sd);
    int ret;

    if (enable) {
        ret = imx708_power_on(&imx708->client->dev);
        if (ret)
            return ret;
        // Sau khi bật nguồn, có thể cần delay và cấu hình lại các thanh ghi
        // imx708_set_default_mode(imx708); // hoặc cấu hình mode trước đó đã set_fmt
    } else {
        ret = imx708_power_off(&imx708->client->dev);
        if (ret)
            return ret;
    }

    return 0;
}

// Video operations (ví dụ: set_fmt)
static int imx708_set_fmt(struct v4l2_subdev *sd,
                           struct v4l2_subdev_state *cfg,
                           struct v4l2_subdev_format *format)
{
    // Logic để cấu hình cảm biến cho định dạng, độ phân giải, crop, field...
    // Dựa vào format->format.width, format->format.height, format->format.code (MEDIA_BUS_FMT_...)
    // Ghi các thanh ghi cảm biến tương ứng để thay đổi mode.
    // Lưu cấu hình mới vào cấu trúc imx708_device nếu cần.
    // v4l2_subdev_state_get_format(cfg, format->pad) = *format; // Lưu cấu hình vào state

    dev_dbg(&imx708->client->dev, "Setting format %dx%d\n", format->format.width, format->format.height);

    // Cần tra cứu trong driver gốc để biết các sequence ghi thanh ghi cho từng mode
    // Ví dụ:
    // if (format->format.width == 4608 && format->format.height == 2592) {
    //    // Ghi sequence thanh ghi cho mode 4608x2592
    // } else if (format->format.width == 2304 && format->format.height == 1296) {
    //    // Ghi sequence thanh ghi cho mode 2304x1296
    // }
    // ...

    return 0; // Trả về 0 nếu thành công, lỗi nếu không hỗ trợ
}

// Các hàm V4L2 operation khác cần triển khai:
// imx708_get_fmt, imx708_enum_mbus_code, imx708_enum_frame_size, imx708_get_selection, imx708_set_selection
// imx708_s_stream (cho VIDIOC_STREAMON/OFF)
// imx708_s_ctrl, imx708_g_ctrl, imx708_queryctrl (cho các V4L2 controls như exposure, gain, v.v.)
// imx708_set_link_freq, imx708_enum_hs_sizes

// --- Khai báo các V4L2 subdev operations ---
static const struct v4l2_subdev_core_ops imx708_core_ops = {
    .s_power = imx708_s_power,
    // Thêm các core ops khác nếu cần
};

static const struct v4l2_subdev_video_ops imx708_video_ops = {
    .s_fmt = imx708_set_fmt,
    // Thêm các video ops khác
};

static const struct v4l2_subdev_pad_ops imx708_pad_ops = {
    .enum_mbus_code = NULL, // Cần triển khai
    .enum_frame_size = NULL, // Cần triển khai
    .get_fmt = NULL, // Cần triển khai
    .set_fmt = imx708_set_fmt, // Có thể dùng chung với video ops s_fmt
    .get_selection = NULL, // Cần triển khai
    .set_selection = NULL, // Cần triển khai
    .set_link_freq = NULL, // Cần triển khai
    .enum_link_freq = NULL, // Cần triển khai
    .enum_hs_sizes = NULL, // Cần triển khai
};


// Tập hợp tất cả các operations của subdev
static const struct v4l2_subdev_ops imx708_subdev_ops = {
    .core = &imx708_core_ops,
    .video = &imx708_video_ops,
    .pad = &imx708_pad_ops,
    // Thêm các ops khác (như .ctrl)
};

// --- I2C Driver Stuff ---

// Hàm probe: được gọi khi kernel phát hiện thiết bị IMX708 trên bus I2C (dựa vào Device Tree)
static int imx708_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct imx708_device *imx708;
    struct device *dev = &client->dev;
    int ret;

    dev_info(dev, "IMX708 sensor probe\n");

    // 1. Cấp phát bộ nhớ cho cấu trúc driver
    imx708 = devm_kzalloc(dev, sizeof(*imx708), GFP_KERNEL);
    if (!imx708)
        return -ENOMEM;

    // 2. Lưu trữ đối tượng I2C client và đặt dữ liệu driver cho device
    imx708->client = client;
    v4l2_i2c_subdev_init(&imx708->sd, client, &imx708_subdev_ops);
    //sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE; // Tùy chọn, nếu muốn tạo /dev/v4l-subdevX
    dev_set_drvdata(dev, imx708); // Lưu con trỏ imx708 vào device

    // 3. Lấy các tài nguyên từ Device Tree
    // Lấy chân reset (nếu có)
    imx708->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(imx708->reset_gpio)) {
        ret = PTR_ERR(imx708->reset_gpio);
        dev_err(dev, "Failed to get reset GPIO: %d\n", ret);
        return ret;
    }
    // Lấy XCLK
    imx708->xclk = devm_clk_get(dev, "xclk");
    if (IS_ERR(imx708->xclk)) {
        ret = PTR_ERR(imx708->xclk);
        dev_err(dev, "Failed to get XCLK: %d\n", ret);
        return ret;
    }
    // Lấy các nguồn cấp điện (cần định nghĩa trong Device Tree)
    // imx708->vana1 = devm_regulator_get(dev, "vana1");
    // if (IS_ERR(imx708->vana1)) { ... return ret; }
    // ... tương tự cho vddl, vdig ...

    // 4. Bật nguồn tạm thời để kiểm tra ID cảm biến
    ret = imx708_power_on(dev);
    if (ret) {
         dev_err(dev, "Failed to power on for ID check\n");
         return ret;
    }

    // 5. Kiểm tra Chip ID
    ret = imx708_check_id(imx708);
    if (ret) {
        dev_err(dev, "Chip ID check failed\n");
        imx708_power_off(dev); // Tắt nguồn đã bật tạm
        return ret;
    }

    // 6. Tắt nguồn sau khi kiểm tra ID
    imx708_power_off(dev);

    // 7. Khởi tạo Media Controller Pad
    imx708->pad.flags = MEDIA_PAD_FL_SOURCE;
    // Đặt pixel format mặc định cho pad (ví dụ: SRGGB10_1X10)
    imx708->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
    media_entity_init(&imx708->sd.entity, 1, &imx708->pad, 0);

    // 8. Đăng ký V4L2 subdev
    ret = v4l2_async_register_subdev(&imx708->sd);
    if (ret) {
        dev_err(dev, "Failed to register v4l2 subdev: %d\n", ret);
        media_entity_cleanup(&imx708->sd.entity);
        return ret;
    }

    dev_info(dev, "IMX708 sensor probe successful\n");

    return 0; // Thành công
}

// Hàm remove: được gọi khi module driver bị gỡ bỏ hoặc thiết bị bị xóa
static void imx708_remove(struct i2c_client *client)
{
    struct imx708_device *imx708 = i2c_get_clientdata(client);

    dev_info(&client->dev, "IMX708 sensor remove\n");

    v4l2_async_unregister_subdev(&imx708->sd);
    media_entity_cleanup(&imx708->sd.entity);

    // Power off nếu driver vẫn đang bật nguồn
    imx708_power_off(&client->dev); // Hàm này nên kiểm tra trạng thái nguồn trước khi tắt

    dev_set_drvdata(&client->dev, NULL);
}

// Danh sách các thiết bị I2C được hỗ trợ bởi driver này (dựa trên compatible string trong Device Tree)
static const struct of_device_id imx708_of_match[] = {
    { .compatible = "sony,imx708" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx708_of_match);

// Định nghĩa cấu trúc I2C driver
static struct i2c_driver imx708_i2c_driver = {
    .driver = {
        .name = "imx708", // Tên driver
        .of_match_table = imx708_of_match, // Kết nối với Device Tree
    },
    .probe = imx708_probe, // Hàm probe
    .remove = imx708_remove, // Hàm remove
};

// Khai báo driver
module_i2c_driver(imx708_i2c_driver);

MODULE_DESCRIPTION("V4L2 driver for Sony IMX708 image sensor");
MODULE_AUTHOR("Your Name"); // Thay thế bằng tên của bạn
MODULE_LICENSE("GPL"); // Giấy phép
