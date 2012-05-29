// vim: tabstop=4 noexpandtab shiftwidth=4 softtabstop=4
using PulseAudio;

/*
 * unity.widgets.systemsettings.*.volumecontrol
 *
 * [ ))) ---------*-------- ]
 *
 * Properties:
 * "type"			- string = "x-system-settings"
 * "x-tablet-widget	- string = "unity.widget.systemsettings.tablet.volumecontrol"
 * "action"         - string = "volume"
 *
 * The volume value has to be taken off the GAction state
 * "volume"         - double [0.0..1.0]
 *
 */

namespace Unity.Settings
{
	[CCode(cname="pa_cvolume_set", cheader_filename = "pulse/volume.h")]
	extern unowned PulseAudio.CVolume? vol_set (PulseAudio.CVolume? cv, uint channels, PulseAudio.Volume v);

	public class AudioControl : Object
	{
		private PulseAudio.GLibMainLoop loop;
		private PulseAudio.Context context;
		private bool   _mute;
		private double _volume;

		public signal void ready ();
		public signal void mute_toggled (bool mute);
		public signal void volume_changed (double v);

		public AudioControl ()
		{
			loop = new PulseAudio.GLibMainLoop ();

			var props = new Proplist ();
			props.sets (Proplist.PROP_APPLICATION_NAME, "Ubuntu Audio Settings");
			props.sets (Proplist.PROP_APPLICATION_ID, "com.ubuntu.audiosettings");
			props.sets (Proplist.PROP_APPLICATION_ICON_NAME, "multimedia-volume-control");
			props.sets (Proplist.PROP_APPLICATION_VERSION, "0.1");

			context = new PulseAudio.Context (loop.get_api(), null, props);

			context.set_state_callback (notify_cb);

			if (context.connect(null, Context.Flags.NOFAIL, null) < 0)
			{
				warning( "pa_context_connect() failed: %s\n", PulseAudio.strerror(context.errno()));
				return;
			}
		}

		/* PulseAudio logic*/
		private void notify_cb (Context c)
		{
			if (c.get_state () == Context.State.READY)
			{
				ready ();
			}
		}

		/* Mute operations */
		private void toggle_mute_success (Context c, int success)
		{
			if ((bool)success)
				mute_toggled (_mute);
		}

		private void toggle_mute_cb (Context c, SinkInfo? i, int eol)
		{
			if (i == null)
				return;

			_mute = ! (bool) i.mute;
			c.set_sink_mute_by_index (i.index, _mute, toggle_mute_success);
		}

		public void switch_mute ()
		{
			if (context.get_state () != Context.State.READY)
			{
				warning ("Could not mute: PulseAudio server connection is not ready.");
				return;
			}

			context.get_sink_info_by_index (0, toggle_mute_cb);
		}

		/* Volume operations */
		private void set_volume_success_cb (Context c, int success)
		{
			if ((bool)success)
				volume_changed (_volume);
		}

		private void sink_info_set_volume_cb (Context c, SinkInfo? i, int eol)
		{
			if (i == null)
				return;

			double tmp = (double)(PulseAudio.Volume.NORM - PulseAudio.Volume.MUTED) * _volume;
			PulseAudio.Volume vol = (PulseAudio.Volume)tmp + PulseAudio.Volume.MUTED;
			unowned CVolume cvol = vol_set (i.volume, 1, vol);
			c.set_sink_volume_by_index (i.index, cvol, set_volume_success_cb);
		}

		private void server_info_cb_for_set_volume (Context c, ServerInfo? i)
		{
			if (i == null)
			{
				warning ("Could not get PulseAudio server info");
				return;
			}

			context.get_sink_info_by_name (i.default_sink_name, sink_info_set_volume_cb);
		}

		public void set_volume (double volume)
		{
			if (context.get_state () != Context.State.READY)
			{
				warning ("Could not change volume: PulseAudio server connection is not ready.");
				return;
			}
			_volume = volume;

			context.get_server_info (server_info_cb_for_set_volume);
		}

		public void get_volume ()
		{
		}

	}


	public class AudioMenu : Application
	{
		private DBusConnection conn;
		private GLib.Menu gmenu;
		private GLib.SimpleActionGroup ag;
		private AudioControl ac;
		private bool _ready = false;

		public AudioMenu ()
		{
			Object (application_id: "com.ubuntu.audiosettings");
			//flags = ApplicationFlags.IS_SERVICE;

			gmenu = new Menu ();
			ag   = new SimpleActionGroup ();

			ac = new AudioControl ();
			ac.ready.connect (ready_cb);
			ac.volume_changed.connect (volume_changed_cb);
		}

		private void state_changed_cb (ActionGroup action_group, string name, Variant val)
		{
			if (name == "volume")
			{
				ac.set_volume (val.get_double ());
			}
		}

		private void bootstrap_actions ()
		{
			//TODO: Set the state accordingly
			var mute = new SimpleAction.stateful ("mute", new VariantType("b"),  new Variant.boolean(true));
			var volume = new SimpleAction.stateful ("volume", new VariantType("d"), new Variant.double(0.9));
			ag.insert (mute);
			ag.insert (volume);

			ag.action_state_changed.connect (state_changed_cb);
		}

		private void bootstrap_menu ()
		{
			var volume_control = new MenuItem (null, null);
			volume_control.set_attribute ("type", "s", "x-system-settings");
			volume_control.set_attribute ("x-tablet-widget", "s", "unity.widgets.systemsettings.tablet.volumecontrol");
			volume_control.set_attribute (GLib.Menu.ATTRIBUTE_ACTION, "s", "volume");

			gmenu.append_item (volume_control);
		}

		private void ready_cb ()
		{
			if (_ready)
				return;

			_ready = true;

			bootstrap_actions ();
			bootstrap_menu ();

			try
			{
				conn = Bus.get_sync (BusType.SESSION, null);
			}
			catch (IOError e)
			{
				warning ("Could not connect to the session bus");
				return;
			}

			try
			{
				conn.export_menu_model ("/com/ubuntu/audiosettings", gmenu);
				conn.export_action_group ("/com/ubuntu/audiosettings/actions", ag);
			}
			catch (GLib.Error e)
			{
				warning ("Menu model and/or action group could not be exported.");
				return;
			}

		}

		private void volume_changed_cb (double vol)
		{
		}

		public static int main (string[] args)
		{
			var menu = new AudioMenu ();
			menu.hold ();

			return menu.run (args);
		}
	}
}