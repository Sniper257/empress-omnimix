/* R160: request configuration descriptors with room for an EP0 packet.
 * No VID/PID exceptions, pool changes, reset changes or suppressed overruns.
 * Runs under the existing host lock, using the existing descriptor allocation.
 */
static void fetchConfigDescriptors(UsbdIoRequest_t *req)
{
	UsbdDevice_t *dev = req->m_correspEndpoint->m_correspDevice;
	const UsbDeviceDescriptor *device = dev->m_staticDeviceDescPtr;
	unsigned counter = dev->m_fetchDescriptorCounter;
	unsigned packet = req->m_correspEndpoint->m_hcEd->m_hcArea.stru.m_maxPacketSize & 0x7FF;
	unsigned used = (u8 *)dev->m_staticDeviceDescEndPtr - (u8 *)dev->m_staticDeviceDescPtr;
	unsigned capacity = usbConfig.m_maxStaticDescSize;
	unsigned total = 0, actual, readLen, config;
	u8 *dest = dev->m_staticDeviceDescEndPtr;

	if (used > capacity || (packet != 8 && packet != 16 && packet != 32 && packet != 64))
		goto invalid;
	if (counter)
	{
		/* A short packet is valid only when the required descriptor is complete. */
		actual = req->m_transferedBytes;
		if ((req->m_resultCode != USB_RC_OK && req->m_resultCode != USB_RC_DATAUNDER) ||
			actual > req->m_length || actual > capacity - used || actual < 9)
			goto invalid;
		total = READ_UINT16(dest + 2);
		if (dest[0] < 9 || dest[1] != USB_DT_CONFIG || total < dest[0])
			goto invalid;
		if (!(counter & 1))
		{
			unsigned offset = 0;
			if (actual < total)
				goto invalid;
			/* Bound every descriptor before exposing the chain to USB drivers. */
			while (offset < total)
			{
				if (total - offset < 2 || dest[offset] < 2 || dest[offset] > total - offset)
					goto invalid;
				offset += dest[offset];
			}
			used += total;
			dest += total;
			dev->m_staticDeviceDescEndPtr = dest;
		}
	}
	config = counter >> 1;
	if (config >= device->bNumConfigurations)
	{
		dev->m_reportDescriptorStart = NULL;
		if (usbConfig.m_curDescNum)
		{
			handleStaticDeviceDescriptor(dev, (UsbDeviceDescriptor *)dev->m_staticDeviceDescPtr,
				(UsbDeviceDescriptor *)dev->m_staticDeviceDescEndPtr);
			if (dev->m_reportDescriptorStart)
			{
				dev->m_reportDescriptorCurForFetch = dev->m_reportDescriptorStart;
				fetchNextReportDescriptor(req);
				return;
			}
		}
		connectNewDevice(dev);
		return;
	}
	/* Read a complete nine-byte header; make room for at least one EP0 packet.
	 * On the second pass, wTotalLength may be smaller than the packet size. */
	readLen = (counter & 1) ? total : 9;
	if (readLen < packet)
		readLen = packet;
	if (readLen > capacity - used)
	{
		ud_note(11, dev, readLen, capacity, 0);
		return;
	}
	dev->m_fetchDescriptorCounter = counter + 1;
	doControlTransfer(req->m_correspEndpoint, &dev->m_ioRequest,
		USB_DIR_IN | USB_RECIP_DEVICE, USB_REQ_GET_DESCRIPTOR,
		config | (USB_DT_CONFIG << 8), 0, readLen, dest, fetchConfigDescriptors);
	return;
invalid:
	killDevice(dev, req->m_correspEndpoint);
}
