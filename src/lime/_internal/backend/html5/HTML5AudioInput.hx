package lime._internal.backend.html5;

import haxe.io.Bytes;
import lime.media.AudioInput;
import lime.media.AudioInputDevice;

class HTML5AudioInput
{
	private var parent:AudioInput;

	public static function getDefaultDevice():AudioInputDevice
	{
		return null;
	}

	public static function getDevices():Array<AudioInputDevice>
	{
		return [];
	}

	public static function isSupported():Bool
	{
		return false;
	}

	public function new(parent:AudioInput)
	{
		this.parent = parent;
	}

	public function dispose():Void {}

	public function getSamplesAvailable():Int
	{
		return 0;
	}

	public function read(buffer:Bytes, samples:Int):Int
	{
		return 0;
	}

	public function start():Bool
	{
		return false;
	}

	public function stop():Void {}
}
