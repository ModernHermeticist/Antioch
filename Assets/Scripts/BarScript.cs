using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

public class BarScript : MonoBehaviour
{
    [SerializeField]
    private float fillAmount;

    [SerializeField]
    private Image content;

    public float MaxValue { get; set; }

    public float Value
    {
        set
        {
            Debug.Log("value: " + value);
            Debug.Log("fillAmount before mapping: " + fillAmount);
            if (fillAmount != content.fillAmount)
            {
                fillAmount = Map(value, 0, MaxValue, 0, 1);
            }
            Debug.Log("fillAmount after mapping: " + fillAmount);
        }
    }

	void Start ()
    {
		
	}
	

	void Update ()
    {
        HandleBar();
	}

    private void HandleBar()
    {
        content.fillAmount = fillAmount;
    }

    private float Map(float value, float inMin, float inMax, float outMin = 0, float outMax = 1)
    {
        return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
    }
}
