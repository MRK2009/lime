package lime.graphics.vulkan;

class VKGraphicsPipelineInfo
{
	public var alphaBlendOp:Int = VK.BLEND_OP_ADD;
	public var colorBlendOp:Int = VK.BLEND_OP_ADD;
	public var cullMode:Int = VK.CULL_MODE_BACK_BIT;
	public var depthCompareOp:Int = VK.COMPARE_OP_LESS;
	public var depthTest:Bool = false;
	public var depthWrite:Bool = false;
	public var dstAlphaBlendFactor:Int = VK.BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	public var dstColorBlendFactor:Int = VK.BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	public var dynamicStateFlags:Int = VK.DYNAMIC_STATE_VIEWPORT_BIT | VK.DYNAMIC_STATE_SCISSOR_BIT;
	public var frontFace:Int = VK.FRONT_FACE_COUNTER_CLOCKWISE;
	public var polygonMode:Int = VK.POLYGON_MODE_FILL;
	public var primitiveRestart:Bool = false;
	public var srcAlphaBlendFactor:Int = VK.BLEND_FACTOR_ONE;
	public var srcColorBlendFactor:Int = VK.BLEND_FACTOR_SRC_ALPHA;
	public var topology:Int = VK.PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	public var vertexAttributes:Array<VKVertexAttribute>;
	public var vertexBindings:Array<VKVertexBinding>;
	public var blend:Bool = false;

	public function new()
	{
		vertexAttributes = [];
		vertexBindings = [];
	}

	@:allow(lime.graphics.vulkan.VKDevice)
	private function pack(cacheHandle:haxe.Int64 = null):Array<Int>
	{
		if (cacheHandle == null)
		{
			cacheHandle = haxe.Int64.ofInt(0);
		}
		var result = [
			cacheHandle.high,
			cacheHandle.low,
			topology,
			primitiveRestart ? 1 : 0,
			polygonMode,
			cullMode,
			frontFace,
			depthTest ? 1 : 0,
			depthWrite ? 1 : 0,
			depthCompareOp,
			blend ? 1 : 0,
			srcColorBlendFactor,
			dstColorBlendFactor,
			colorBlendOp,
			srcAlphaBlendFactor,
			dstAlphaBlendFactor,
			alphaBlendOp,
			dynamicStateFlags,
			vertexBindings.length
		];

		for (binding in vertexBindings)
		{
			result.push(binding.binding);
			result.push(binding.stride);
			result.push(binding.inputRate);
		}

		result.push(vertexAttributes.length);
		for (attribute in vertexAttributes)
		{
			result.push(attribute.location);
			result.push(attribute.binding);
			result.push(attribute.format);
			result.push(attribute.offset);
		}

		return result;
	}
}
