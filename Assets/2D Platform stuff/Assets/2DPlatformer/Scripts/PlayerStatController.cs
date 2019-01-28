using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class PlayerStatController : MonoBehaviour 
{
	// Player Stats
	[SerializeField]
	private float curHp = 100.0f;
	[SerializeField]
	private float maxHp = 100.0f;
	[SerializeField]
	private float curStam = 45.0f;
	[SerializeField]
	private float maxStam = 45.0f;
	[SerializeField]
	private int curEssence = 10;


	public float HpValue
	{
		get
		{
			return curHp;
		}

		set
		{
			curHp = value;
		}
	}

	public float MaxHpValue
	{
		get
		{
			return maxHp;
		}

		set
		{
			maxHp = value;
		}
	}

	public float StamValue
	{
		get
		{
			return curStam;
		}

		set
		{
			curStam = value;
		}
	}

	public float MaxStamValue
	{
		get
		{
			return maxStam;
		}
		
		set
		{
			maxStam = value;
		}
	}

	public int EssenceValue
	{
		get
		{
			return curEssence;
		}

		set
		{
			curEssence = value;
		}
	}
	void Start () 
	{
		
	}
	
	void Update () 
	{
		
	}

}
